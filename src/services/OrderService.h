// ============================================================================
// 订单服务 —— 事务边界所在层（CLAUDE.md 4.1）
//
// 线程模型（CLAUDE.md 4.1/6.6）：所有方法都在工作线程池线程上被调用，
// 内部执行同步事务。本类无任何可变状态（DAO 均为无状态对象），
// 可安全地被多个池线程并发使用 —— 因此 Controller 侧按请求创建实例。
// ============================================================================
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
//   kExecuted —— 本次请求真实执行业务，result 为完整结果；
//   kCached   —— 幂等命中：首次成功时的响应快照（逐字节一致），
//                由 Controller 原样写回，不重新解析。
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

    // 创建订单：幂等抢占 → 商品校验 → 防超卖条件扣减 → 快照单价落订单 →
    // 幂等快照写回，全部在同一事务内（亮点①②，CLAUDE.md 4.1/5.6/6.1）。
    CreateOrderOutcome createOrder(const models::CreateOrderReq& req);

private:
    std::unique_ptr<dao::ProductDao>    productDao_;
    std::unique_ptr<dao::OrderDao>      orderDao_;
    std::unique_ptr<dao::IdempotentDao> idempotentDao_;
};

}  // namespace services
