// ============================================================================
// 支付服务 —— 事务边界所在层（CLAUDE.md 4.1）
//
// 线程模型（同 OrderService）：方法在工作线程池线程上被调用，内部同步事务；
// 本类无可变状态，可被多池线程并发使用。
//
// 回调处理流水线（全部在同一事务内）：
//   验签（事务外）→ 订单存在性 + 金额核对 → 渠道幂等落库（uk_channel_trade）
//   → 订单状态流转（WHERE status = 0 条件更新）→ 账户出账（条件扣减）
//   → 写流水（uk_request_biz 防重复记账）→ 本地消息表（链路④前置）
// ============================================================================
#pragma once

#include <memory>

#include <jsoncpp/json/json.h>

#include "models/Payment.h"
#include "models/Result.h"

namespace dao {
class AccountDao;
class LocalMessageDao;
class OrderDao;
class PaymentDao;
}  // namespace dao

namespace services {

class PaymentService {
public:
    PaymentService();
    ~PaymentService();

    // 处理渠道支付回调。rawBody 为回调原文（落库留证）。
    // 成功返回 {"orderNo": "...", "status": 1}；渠道以 code=0 作为回调受理 ack。
    models::Result<Json::Value> handleCallback(const models::PaymentCallbackReq& req,
                                               const std::string& rawBody);

private:
    std::unique_ptr<dao::PaymentDao>      paymentDao_;
    std::unique_ptr<dao::AccountDao>      accountDao_;
    std::unique_ptr<dao::OrderDao>        orderDao_;
    std::unique_ptr<dao::LocalMessageDao> localMessageDao_;
};

}  // namespace services
