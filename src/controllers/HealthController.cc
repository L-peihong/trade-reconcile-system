#include "controllers/HealthController.h"

#include <string>

#include <jsoncpp/json/json.h>

#include "common/Logger.h"
#include "models/Result.h"

namespace controllers {

void HealthController::health(
    const drogon::HttpRequestPtr& request,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    // GET 不强制要求 X-Request-Id（CLAUDE.md 4.2），带了就回显、用于串日志，
    // 没带则为空串，不影响响应结构。
    const std::string requestId = request->getHeader("X-Request-Id");

    if (!requestId.empty()) {
        common::Logger::withRequestId(requestId)->debug("GET /health");
    }

    Json::Value payload;
    payload["status"] = "up";

    const auto result = models::Result<Json::Value>::ok(payload, requestId);

    // newHttpJsonResponse 会自动设置 Content-Type: application/json，
    // 并默认返回 200 —— 业务层面的成败由 body 里的 code 表达，
    // HTTP 状态码只用来表达传输层语义（见 CLAUDE.md 4.3）。
    auto response = drogon::HttpResponse::newHttpJsonResponse(result.toJson());
    callback(response);
}

}  // namespace controllers
