// 订单服务，事务边界所在层。
// 方法都跑在工作线程池线程上，内部走同步事务；本类无状态，线程安全。
#pragma once

#include <memory>
#include <string>

#include "models/Order.h"
#include "models/Result.h"

namespace dao {
class IdempotentDao;
class OrderDao;
class ProductDao;
}  // namespace dao

namespace services {

// createOrder 的返回形态：
//   kExecuted —— 本次真实执行业务，result 为完整结果；
//   kCached   —— 幂等命中，返回首次成功时的响应快照，由 Controller 原样写回。
struct CreateOrderOutcome {
    enum class Kind { kExecuted, kCached };
    Kind kind = Kind::kExecuted;
    models::Result<models::Order> result;
    std::string cachedBody;  // kCached 时有效：完整响应体 JSON
};

class OrderService {
public:
    OrderService();
    ~OrderService();

    // 创建订单：幂等抢占 → 商品校验 → 条件扣减库存 → 快照单价落订单 →
    // 幂等快照写回，全部在同一事务内。
    CreateOrderOutcome createOrder(const models::CreateOrderReq& req);

private:
    std::unique_ptr<dao::ProductDao>    productDao_;
    std::unique_ptr<dao::OrderDao>      orderDao_;
    std::unique_ptr<dao::IdempotentDao> idempotentDao_;
};

}  // namespace services
