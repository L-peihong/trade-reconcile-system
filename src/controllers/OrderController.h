// POST /api/v1/orders —— 创建订单（幂等 + 防超卖）
// handler 只做参数解析并提交到工作线程池，同步事务不在 IO 循环线程上跑。
#pragma once

#include <drogon/HttpController.h>

#include <functional>

namespace controllers {

class OrderController : public drogon::HttpController<OrderController> {
public:
    METHOD_LIST_BEGIN
    // 过滤器名要写命名空间全限定名（DrObject 注册名即 demangle(typeid) 的结果）
    ADD_METHOD_TO(OrderController::createOrder, "/api/v1/orders",
                  drogon::Post, "common::RequestIdFilter");
    METHOD_LIST_END

    void createOrder(const drogon::HttpRequestPtr& request,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback);
};

}  // namespace controllers
