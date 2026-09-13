// 支付服务，事务边界所在层。方法跑在工作线程池线程上，内部同步事务。
// 回调流水线（除验签外都在同一事务内）：
//   订单存在性 + 金额核对 → 渠道幂等落库 → 状态流转（WHERE status = 0）
//   → 账户出账（条件扣减）→ 写流水 → 本地消息表
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
