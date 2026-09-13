#include "controllers/AdminController.h"

#include <cstdint>
#include <string>
#include <utility>

#include <drogon/drogon.h>
#include <jsoncpp/json/json.h>

#include "common/ErrorCode.h"
#include "common/Logger.h"
#include "common/RequestIdFilter.h"
#include "models/Result.h"
#include "services/ReconcileService.h"
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

// 同步 Service 调用丢线程池，结果切回 IO 线程发响应
void submitToPool(std::function<models::Result<Json::Value>()> work,
                  std::function<void(const drogon::HttpResponsePtr&)> callback,
                  const std::string& requestId) {
    const bool submitted = utils::globalThreadPool().submit(
        [work = std::move(work), callback = std::move(callback)]() mutable {
            const auto result = work();
            drogon::HttpResponsePtr resp =
                drogon::HttpResponse::newHttpJsonResponse(result.toJson());
            drogon::app().getLoop()->queueInLoop(
                [resp, callback = std::move(callback)]() { callback(resp); });
        });

    if (!submitted) {
        const auto body = models::Result<int>::fail(
            common::ErrCode::kInternalError, "服务正在关闭，请稍后重试", requestId);
        auto resp = drogon::HttpResponse::newHttpJsonResponse(body.toJson());
        resp->setStatusCode(drogon::k503ServiceUnavailable);
        callback(resp);
    }
}

}  // namespace

void AdminController::runReconcile(
    const drogon::HttpRequestPtr& request,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    const std::string requestId = common::requestIdOf(request);

    const auto json = request->getJsonObject();
    if (!json || !json->isMember("billDate") || !(*json)["billDate"].isString() ||
        (*json)["billDate"].asString().empty()) {
        callback(badRequest(common::ErrCode::kParamInvalid,
                            "billDate(YYYY-MM-DD) 必填", requestId));
        return;
    }
    const std::string billDate = (*json)["billDate"].asString();

    submitToPool(
        [billDate]() {
            services::ReconcileService service;
            return service.run(billDate);
        },
        std::move(callback), requestId);
}

void AdminController::listDiffs(
    const drogon::HttpRequestPtr& request,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    const std::string requestId = common::requestIdOf(request);
    const std::string batchNo = request->getParameter("batchNo");

    if (batchNo.empty()) {
        callback(badRequest(common::ErrCode::kParamInvalid,
                            "batchNo 查询参数必填", requestId));
        return;
    }

    submitToPool(
        [batchNo]() {
            services::ReconcileService service;
            return service.listDiffs(batchNo, 200);
        },
        std::move(callback), requestId);
}

void AdminController::handleDiff(
    const drogon::HttpRequestPtr& request,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    std::int64_t diffId) {

    const std::string requestId = common::requestIdOf(request);

    const auto json = request->getJsonObject();
    if (!json || !json->isMember("action") || !(*json)["action"].isString()) {
        callback(badRequest(common::ErrCode::kParamInvalid,
                            "action(verify/ignore) 必填", requestId));
        return;
    }
    const std::string action = (*json)["action"].asString();
    const std::string remark =
        json->isMember("remark") ? (*json)["remark"].asString() : "";

    submitToPool(
        [diffId, action, remark]() {
            services::ReconcileService service;
            return service.handleDiff(diffId, action, remark);
        },
        std::move(callback), requestId);
}

}  // namespace controllers
