#include "controllers/PaymentCallbackController.h"

#include <memory>
#include <string>
#include <utility>

#include <drogon/drogon.h>
#include <jsoncpp/json/json.h>

#include "common/ErrorCode.h"
#include "common/Logger.h"
#include "common/RequestIdFilter.h"
#include "models/Payment.h"
#include "models/Result.h"
#include "services/PaymentService.h"
#include "utils/ThreadPool.h"

namespace controllers {
namespace {

drogon::HttpResponsePtr badRequest(common::ErrCode code,
                                   const std::string& message,
                                   const std::string& requestId) {
    const auto body = models::Result<int>::fail(code, message, requestId);
    auto resp = drogon::HttpResponse::newHttpJsonResponse(body.toJson());
    resp->setStatusCode(drogon::k400BadRequest);
    return resp;
}

bool hasNonEmptyString(const Json::Value& json, const char* key) {
    return json.isMember(key) && json[key].isString() && !json[key].asString().empty();
}

}  // namespace

void PaymentCallbackController::handleCallback(
    const drogon::HttpRequestPtr& request,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    const std::string requestId = common::requestIdOf(request);  // filter 已保证合法
    auto log = common::Logger::withRequestId(requestId);

    const auto json = request->getJsonObject();
    if (!json || !hasNonEmptyString(*json, "orderNo") ||
        !hasNonEmptyString(*json, "channel") ||
        !hasNonEmptyString(*json, "channelTradeNo") ||
        !hasNonEmptyString(*json, "amount") ||
        !hasNonEmptyString(*json, "sign") ||
        !json->isMember("timestamp") || !(*json)["timestamp"].isNumeric()) {
        callback(badRequest(common::ErrCode::kParamInvalid,
                            "orderNo/channel/channelTradeNo/amount/timestamp/sign 缺失或非法",
                            requestId));
        return;
    }

    models::PaymentCallbackReq req;
    req.requestId      = requestId;
    req.orderNo        = (*json)["orderNo"].asString();
    req.channel        = (*json)["channel"].asString();
    req.channelTradeNo = (*json)["channelTradeNo"].asString();
    req.amount         = (*json)["amount"].asString();
    req.timestamp      = std::to_string((*json)["timestamp"].asInt64());
    req.sign           = (*json)["sign"].asString();

    const std::string rawBody = std::string(request->body());

    // 线程模型（CLAUDE.md 4.1/6.6）：同步事务丢线程池；lambda 只按值捕获。
    const bool submitted = utils::globalThreadPool().submit(
        [req, rawBody, callback = std::move(callback)]() mutable {
            services::PaymentService service;

            const auto result = service.handleCallback(req, rawBody);

            drogon::HttpResponsePtr resp =
                drogon::HttpResponse::newHttpJsonResponse(result.toJson());
            // 响应切回 IO 循环线程再回调
            drogon::app().getLoop()->queueInLoop(
                [resp, callback = std::move(callback)]() { callback(resp); });
        });

    if (!submitted) {
        log->warn("thread pool stopped, reject request");
        const auto body = models::Result<int>::fail(
            common::ErrCode::kInternalError, "服务正在关闭，请稍后重试", requestId);
        auto resp = drogon::HttpResponse::newHttpJsonResponse(body.toJson());
        resp->setStatusCode(drogon::k503ServiceUnavailable);
        callback(resp);
    }
}

}  // namespace controllers
