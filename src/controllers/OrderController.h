// ============================================================================
// POST /api/v1/orders —— 创建订单（链路①+②：幂等 + 防超卖）
//
// handler 只做参数解析并 submit 到工作线程池（CLAUDE.md 4.1/6.6），
// 同步事务绝不在 IO 循环线程上执行。
// ============================================================================
#pragma once

#include <drogon/HttpController.h>

#include <functional>

namespace controllers {

class OrderController : public drogon::HttpController<OrderController> {
public:
    METHOD_LIST_BEGIN
    // 过滤器名字必须带命名空间全限定（DrObject 注册名 = demangle(typeid)），
    // 见 RequestIdFilter.h。RequestIdFilter 静态初始化期自注册，无需 main 注册。
    ADD_METHOD_TO(OrderController::createOrder, "/api/v1/orders",
                  drogon::Post, "common::RequestIdFilter");
    METHOD_LIST_END

    void createOrder(const drogon::HttpRequestPtr& request,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback);
};

}  // namespace controllers
