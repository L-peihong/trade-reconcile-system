// ============================================================================
// POST /api/v1/mock/notify —— 模拟商户通知落点（链路④消费侧的通知目标）
//
// 行为：正常返回 200 {"code":0}；body 里带 "fail": true 时返回 500 ——
// 用于演示消费者重试与死信链路。
// ============================================================================
#pragma once

#include <drogon/HttpController.h>

#include <functional>

namespace controllers {

class MockNotifyController : public drogon::HttpController<MockNotifyController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(MockNotifyController::notify, "/api/v1/mock/notify",
                  drogon::Post, "common::RequestIdFilter");
    METHOD_LIST_END

    void notify(const drogon::HttpRequestPtr& request,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback);
};

}  // namespace controllers
