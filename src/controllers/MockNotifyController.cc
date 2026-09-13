#include "controllers/MockNotifyController.h"

#include <string>

#include <drogon/drogon.h>
#include <jsoncpp/json/json.h>

#include "common/Logger.h"
#include "common/RequestIdFilter.h"
#include "models/Result.h"

namespace controllers {

void MockNotifyController::notify(
    const drogon::HttpRequestPtr& request,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    const std::string requestId = common::requestIdOf(request);
    auto log = common::Logger::withRequestId(requestId);

    const auto json = request->getJsonObject();
    const std::string orderNo =
        json && json->isMember("orderNo") ? (*json)["orderNo"].asString() : "unknown";

    // 演示用失败开关：商户端模拟故障,用于验证消费者重试与死信
    const bool simulateFailure =
        json && json->isMember("fail") && (*json)["fail"].asBool();

    if (simulateFailure) {
        log->warn("mock merchant returns 500 (simulated failure): order={}", orderNo);
        const auto body =
            models::Result<int>::fail(common::ErrCode::kInternalError,
                                      "mock merchant internal error", requestId);
        auto resp = drogon::HttpResponse::newHttpJsonResponse(body.toJson());
        resp->setStatusCode(drogon::k500InternalServerError);
        callback(resp);
        return;
    }

    log->info("mock merchant notified: order={}", orderNo);
    const auto body = models::Result<int>::ok(1, requestId);
    callback(drogon::HttpResponse::newHttpJsonResponse(body.toJson()));
}

}  // namespace controllers
