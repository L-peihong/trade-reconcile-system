// ============================================================================
// POST /api/v1/payment/callback —— 渠道支付回调（链路③：验签 + 幂等 + 入账）
//
// handler 只解析参数并 submit 到工作线程池（CLAUDE.md 4.1/6.6）。
// ============================================================================
#pragma once

#include <drogon/HttpController.h>

#include <functional>

namespace controllers {

class PaymentCallbackController
    : public drogon::HttpController<PaymentCallbackController> {
public:
    METHOD_LIST_BEGIN
    // 过滤器名必须带命名空间全限定（DrObject 注册名 = demangle(typeid)，见 RequestIdFilter.h）
    ADD_METHOD_TO(PaymentCallbackController::handleCallback,
                  "/api/v1/payment/callback",
                  drogon::Post, "common::RequestIdFilter");
    METHOD_LIST_END

    void handleCallback(const drogon::HttpRequestPtr& request,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback);
};

}  // namespace controllers
