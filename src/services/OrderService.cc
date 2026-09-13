#include "services/OrderService.h"

#include <chrono>
#include <ctime>
#include <memory>
#include <string>

#include <drogon/drogon.h>

#include "common/BizException.h"
#include "common/ErrorCode.h"
#include "common/Logger.h"
#include "dao/IdempotentDao.h"
#include "dao/OrderDao.h"
#include "dao/ProductDao.h"
#include "models/Idempotent.h"
#include "utils/DbUtil.h"
#include "utils/IdGenerator.h"

namespace services {
namespace {

constexpr const char* kCreateOrderApiPath  = "/api/v1/orders";
constexpr std::int64_t kMaxQuantity        = 999;
constexpr int kOrderExpireMinutes          = 30;
constexpr int kIdempotentExpireMinutes     = 24 * 60;

// 应用侧时间格式化（修正 3：expire_time 应用算好随 INSERT 写入，不回读 DB）。
// 时区策略：显式 UTC+8（北京时间），**不依赖进程 TZ** —— 实测容器内
// TZ=Asia/Shanghai 环境变量未生效（日志仍 UTC），依赖 localtime 会与 MySQL
// --default-time-zone=+8:00 差 8 小时。中国无夏令时，固定偏移精确（CLAUDE.md 6.7）。
std::string dbTimeAfter(int minutes) {
    const auto tp = std::chrono::system_clock::now() +
                    std::chrono::minutes(minutes) + std::chrono::hours(8);
    const std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tmBuf{};
    gmtime_r(&t, &tmBuf);  // UTC 基准 + 8 小时 = 北京时间
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmBuf);
    return std::string(buf);
}

models::Idempotent buildIdempotentRecord(const models::CreateOrderReq& req) {
    models::Idempotent record;
    record.requestId   = req.requestId;
    record.userId      = req.userId;
    record.apiPath     = kCreateOrderApiPath;
    record.requestHash = req.requestHash;
    record.expireTime  = dbTimeAfter(kIdempotentExpireMinutes);
    return record;
}

}  // namespace

OrderService::OrderService()
    : productDao_(std::make_unique<dao::ProductDao>()),
      orderDao_(std::make_unique<dao::OrderDao>()),
      idempotentDao_(std::make_unique<dao::IdempotentDao>()) {
    // 构造里不能碰 drogon::app()：本对象可能随 Controller 的静态注册被构造，
    // 那时 app 尚未初始化。DB 客户端一律在 createOrder() 运行时获取。
}

OrderService::~OrderService() = default;

CreateOrderOutcome OrderService::createOrder(const models::CreateOrderReq& req) {
    auto log = common::Logger::withRequestId(req.requestId);

    // 领域校验（Controller 已做一层，这里兜底 —— 不能信任调用方）
    if (req.quantity <= 0 || req.quantity > kMaxQuantity) {
        log->warn("createOrder rejected: quantity={} out of range", req.quantity);
        CreateOrderOutcome o;
        o.result = models::Result<models::Order>::fail(
            common::ErrCode::kParamInvalid, "购买数量必须在 1~999 之间",
            req.requestId);
        return o;
    }

    try {
        // 事务 RAII（CLAUDE.md 4.1）：任何异常路径退出都会回滚。
        // 本方法运行于工作线程池线程 —— 同步 DB API 禁止在事件循环线程调用。
        utils::TransactionGuard txGuard(utils::beginTransaction());
        auto tx = txGuard.tx();

        // 1. 幂等抢占（亮点②，CLAUDE.md 5.6 语义边界）
        //    与业务同事务：下方任何失败回滚，幂等键一并释放。
        const auto outcome =
            idempotentDao_->tryAcquire(tx, buildIdempotentRecord(req));
        if (outcome.kind == dao::IdempotentOutcome::Kind::kCachedSuccess) {
            log->info("idempotent hit, return cached response");
            CreateOrderOutcome o;
            o.kind       = CreateOrderOutcome::Kind::kCached;
            o.cachedBody = outcome.cachedResponseBody;
            return o;  // 未 commit → 析构回滚（本事务无写入）
        }
        if (outcome.kind == dao::IdempotentOutcome::Kind::kProcessing) {
            log->warn("duplicate request still processing");
            CreateOrderOutcome o;
            o.result = models::Result<models::Order>::fail(
                common::ErrCode::kDuplicateRequest,
                common::toMessage(common::ErrCode::kDuplicateRequest),
                req.requestId);
            return o;
        }

        // 2. 商品校验 + 读取单价快照
        auto product = productDao_->getById(tx, req.productId);
        if (!product.has_value()) {
            throw common::BizException(common::ErrCode::kProductNotFound,
                                       "商品不存在");
        }
        if (!product->onSale()) {
            throw common::BizException(common::ErrCode::kProductNotFound,
                                       "商品已下架");
        }

        // 3. 防超卖条件扣减（亮点①，CLAUDE.md 6.1）
        //    affectedRows 是唯一可信判据，不是补查结果。
        if (productDao_->deductStock(tx, req.productId, req.quantity) == 0) {
            throw common::BizException(common::ErrCode::kStockNotEnough,
                                       "库存不足");
        }

        // 4. 组装订单：快照单价、雪花单号、expire_time 应用侧计算（修正 3）
        models::Order order;
        order.orderNo        = utils::IdGenerator::nextId();
        order.requestId      = req.requestId;
        order.userId         = req.userId;
        order.productId      = req.productId;
        order.quantity       = req.quantity;
        order.unitPriceFen   = product->priceFen;
        order.totalAmountFen = product->priceFen * req.quantity;
        order.status         = models::OrderStatus::kPendingPayment;
        order.expireTime     = dbTimeAfter(kOrderExpireMinutes);
        orderDao_->insert(tx, order);

        // 5. 幂等快照写回（同事务：业务成功才留痕，失败一起回滚）
        //    快照必须与首次在线响应的字节形态一致：drogon 的 newHttpJsonResponse
        //    用 StreamWriterBuilder（commentStyle=None，默认紧凑）序列化，
        //    这里用相同配置，保证重放响应与首次响应**逐字节一致**。
        //    （2026-09-13 实测发现 toStyledString 是美化格式，重放时字节不同，已修）
        const auto response =
            models::Result<models::Order>::ok(order, req.requestId);
        Json::StreamWriterBuilder builder;
        builder["commentStyle"] = "None";
        builder["indentation"] = "";   // 默认是 "\t"(美化),必须显式置空才紧凑
        idempotentDao_->markSuccess(
            tx, req.requestId, Json::writeString(builder, response.toJson()));

        // 6. 显式提交（CLAUDE.md 4.1）：失败抛异常 → 析构兜底回滚
        txGuard.commit();

        log->info("order created: order_no={} product_id={} qty={} amount_fen={}",
                  order.orderNo, req.productId, req.quantity,
                  order.totalAmountFen);

        CreateOrderOutcome o;
        o.result = response;
        return o;

    } catch (const common::BizException& e) {
        log->warn("createOrder failed: code={} msg={}", e.intCode(), e.what());
        CreateOrderOutcome o;
        o.result =
            models::Result<models::Order>::fail(e.code(), e.what(), req.requestId);
        return o;
    } catch (const drogon::orm::DrogonDbException& e) {
        // 技术细节进日志，message 只给通用话术（CLAUDE.md 4.3）
        // DrogonDbException 没有 what()，接口是 base().what()
        log->error("createOrder db error: {}", e.base().what());
        CreateOrderOutcome o;
        o.result = models::Result<models::Order>::fail(
            common::ErrCode::kDbError,
            common::toMessage(common::ErrCode::kDbError), req.requestId);
        return o;
    } catch (const std::exception& e) {
        log->error("createOrder unexpected error: {}", e.what());
        CreateOrderOutcome o;
        o.result = models::Result<models::Order>::fail(
            common::ErrCode::kInternalError,
            common::toMessage(common::ErrCode::kInternalError), req.requestId);
        return o;
    }
}

}  // namespace services
