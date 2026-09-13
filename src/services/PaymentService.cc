#include "services/PaymentService.h"

#include <chrono>
#include <ctime>
#include <memory>
#include <string>

#include <drogon/drogon.h>

#include "common/BizException.h"
#include "common/ErrorCode.h"
#include "common/Logger.h"
#include "dao/AccountDao.h"
#include "dao/LocalMessageDao.h"
#include "dao/OrderDao.h"
#include "dao/PaymentDao.h"
#include "models/Account.h"
#include "models/LocalMessage.h"
#include "models/Order.h"
#include "utils/DbUtil.h"
#include "utils/IdGenerator.h"
#include "utils/MoneyUtil.h"
#include "utils/SignUtil.h"

namespace services {
namespace {

constexpr const char* kSupportedChannel = "MOCK";
constexpr const char* kBizTypeOrderPaid = "ORDER_PAID";

// 渠道秒级时间戳 → 北京时间字符串（YYYY-MM-DD HH:MM:SS）。
// 显式 UTC+8、不依赖进程 TZ（CLAUDE.md 6.7；与 OrderService::dbTimeAfter 同原则）。
std::string epochToBeijing(const std::string& epochSeconds) {
    const std::time_t t = static_cast<std::time_t>(std::stoll(epochSeconds)) + 8 * 3600;
    std::tm tmBuf{};
    gmtime_r(&t, &tmBuf);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmBuf);
    return std::string(buf);
}

std::string configValue(const Json::Value& customConfig,
                        const char* section,
                        const char* key,
                        const std::string& fallback) {
    if (customConfig.isMember(section) && customConfig[section].isMember(key)) {
        return customConfig[section][key].asString();
    }
    return fallback;
}

}  // namespace

PaymentService::PaymentService()
    : paymentDao_(std::make_unique<dao::PaymentDao>()),
      accountDao_(std::make_unique<dao::AccountDao>()),
      orderDao_(std::make_unique<dao::OrderDao>()),
      localMessageDao_(std::make_unique<dao::LocalMessageDao>()) {
    // 构造不碰 drogon::app()（同 OrderService 的原因）。
}

PaymentService::~PaymentService() = default;

models::Result<Json::Value> PaymentService::handleCallback(
    const models::PaymentCallbackReq& req,
    const std::string& rawBody) {

    auto log = common::Logger::withRequestId(req.requestId);

    // ---- 0. 渠道白名单（V1 只支持 MOCK）----
    if (req.channel != kSupportedChannel) {
        return models::Result<Json::Value>::fail(
            common::ErrCode::kUnsupportedChannel, "不支持的支付渠道", req.requestId);
    }

    // ---- 1. 验签（事务外，先挡掉伪造回调再开事务）----
    const auto& customConfig = drogon::app().getCustomConfig();
    const std::string secret =
        configValue(customConfig, "mock_channel", "secret", "");
    if (secret.empty() ||
        !utils::verifyMockSign(secret, req.orderNo, req.channelTradeNo,
                               req.amount, req.timestamp, req.sign)) {
        log->warn("mock sign verify failed: secret_len={} sign_len={} order=[{}] trade=[{}] amount=[{}] ts=[{}]",
                  secret.size(), req.sign.size(), req.orderNo, req.channelTradeNo,
                  req.amount, req.timestamp);
        return models::Result<Json::Value>::fail(
            common::ErrCode::kSignVerifyFailed, "签名校验失败", req.requestId);
    }

    try {
        utils::TransactionGuard txGuard(utils::beginTransaction());
        auto tx = txGuard.tx();

        // ---- 2. 订单存在性 + 金额核对 ----
        auto order = orderDao_->getByOrderNo(tx, req.orderNo);
        if (!order.has_value()) {
            throw common::BizException(common::ErrCode::kOrderNotFound, "订单不存在");
        }
        const auto amountFen = utils::yuanStringToFen(req.amount);
        if (!amountFen.has_value() || *amountFen != order->totalAmountFen) {
            // 金额不一致一律拒 —— 宁可让渠道重试，不能错账（CLAUDE.md 4.3/6.5）
            throw common::BizException(common::ErrCode::kAmountMismatch,
                                       "支付金额与订单金额不一致");
        }

        // ---- 3. 渠道幂等落库（uk_channel_trade）----
        models::Payment payment;
        payment.paymentNo      = utils::IdGenerator::nextId();
        payment.orderNo        = req.orderNo;
        payment.channel        = req.channel;
        payment.channelTradeNo = req.channelTradeNo;
        payment.amountFen      = *amountFen;
        payment.callbackTime   = epochToBeijing(req.timestamp);
        payment.rawBody        = rawBody;

        const auto dedup = paymentDao_->insertWithDedup(tx, payment);
        if (dedup == dao::PaymentInsertOutcome::kDuplicate) {
            // 渠道重复回调是正常现象。同一订单 → 幂等返回成功；
            // 绑定其他订单 → 数据异常，拒（对账环节会进一步暴露）。
            auto existing =
                paymentDao_->getByChannelTradeNo(tx, req.channel, req.channelTradeNo);
            if (existing.has_value() && existing->orderNo == req.orderNo) {
                log->info("channel callback replay, order already processed");
                return models::Result<Json::Value>::ok(
                    Json::Value(), req.requestId);  // 未 commit → 回滚（本事务无写入）
            }
            throw common::BizException(common::ErrCode::kChannelTradeConflict,
                                       "渠道交易号已绑定其他订单");
        }

        // ---- 4. 订单状态流转（条件更新防并发重复入账）----
        if (orderDao_->markPaid(tx, req.orderNo) == 0) {
            auto current = orderDao_->getByOrderNo(tx, req.orderNo);
            if (current.has_value() &&
                current->status == models::OrderStatus::kPaid) {
                // 并发下另一笔回调已抢先支付成功。本回调的支付单随回滚丢弃，
                // 渠道侧的重复交易留待对账暴露（渠道多单）。见 CLAUDE.md 6.9。
                log->warn("order already paid by another callback, rollback this one");
                return models::Result<Json::Value>::ok(
                    Json::Value(), req.requestId);
            }
            throw common::BizException(common::ErrCode::kOrderStatusInvalid,
                                       "订单状态不允许支付");
        }

        // ---- 5. 账户出账（条件扣减，同防超卖思路）+ 流水 ----
        auto account = accountDao_->getByUserId(tx, order->userId);
        if (!account.has_value()) {
            throw common::BizException(common::ErrCode::kAccountNotFound, "账户不存在");
        }
        if (accountDao_->deduct(tx, account->accountNo, *amountFen) == 0) {
            throw common::BizException(common::ErrCode::kBalanceNotEnough, "余额不足");
        }

        // 扣减成功后回读余额算流水前后值：before = after + amount 是确定关系，
        // 因为扣减是原子条件更新且我们刚成功执行了它 —— 无并发读脏问题。
        auto after = accountDao_->getByUserId(tx, order->userId);

        models::AccountFlow flow;
        flow.accountNo = account->accountNo;
        flow.bizType   = models::FlowBizType::kPayment;
        flow.direction = models::FlowDirection::kOut;
        flow.amountFen = *amountFen;
        flow.afterFen  = after->balanceFen;
        flow.beforeFen = after->balanceFen + *amountFen;
        flow.orderNo   = req.orderNo;
        flow.requestId = req.requestId;  // uk_request_biz 防同一回调重复记账
        accountDao_->insertFlow(tx, flow);

        // ---- 6. 本地消息表（链路④前置：与业务同事务，CLAUDE.md 6.3）----
        const auto& rabbitCfg = customConfig["rabbitmq"];
        models::LocalMessage message;
        message.messageId  = utils::IdGenerator::nextId();
        message.bizType    = kBizTypeOrderPaid;
        message.bizId      = req.orderNo;
        message.exchange   = rabbitCfg.isMember("exchange")
                                 ? rabbitCfg["exchange"].asString()
                                 : "trade.exchange";
        message.routingKey = rabbitCfg.isMember("routing_key_notify")
                                 ? rabbitCfg["routing_key_notify"].asString()
                                 : "order.paid";
        message.payload    = order->toJson().toStyledString();
        localMessageDao_->insert(tx, message);

        // ---- 7. 显式提交（CLAUDE.md 4.1）----
        txGuard.commit();

        log->info("payment callback processed: order_no={} amount_fen={} trade_no={}",
                  req.orderNo, *amountFen, req.channelTradeNo);

        Json::Value data;
        data["orderNo"] = req.orderNo;
        data["status"]  = 1;  // 已支付
        return models::Result<Json::Value>::ok(std::move(data), req.requestId);

    } catch (const common::BizException& e) {
        log->warn("payment callback failed: code={} msg={}", e.intCode(), e.what());
        return models::Result<Json::Value>::fail(e.code(), e.what(), req.requestId);
    } catch (const drogon::orm::DrogonDbException& e) {
        log->error("payment callback db error: {}", e.base().what());
        return models::Result<Json::Value>::fail(
            common::ErrCode::kDbError,
            common::toMessage(common::ErrCode::kDbError), req.requestId);
    } catch (const std::exception& e) {
        log->error("payment callback unexpected error: {}", e.what());
        return models::Result<Json::Value>::fail(
            common::ErrCode::kInternalError,
            common::toMessage(common::ErrCode::kInternalError), req.requestId);
    }
}

}  // namespace services
