// POST /api/v1/mock/notify —— 模拟商户通知落点
// 正常返回 200；body 带 "fail": true 时返回 500，用来演示重试与死信链路。
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
