#include "controllers/OrderController.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <drogon/drogon.h>
#include <jsoncpp/json/json.h>
#include <openssl/evp.h>

#include "common/ErrorCode.h"
#include "common/Logger.h"
#include "common/RequestIdFilter.h"
#include "models/Order.h"
#include "models/Result.h"
#include "services/OrderService.h"
#include "utils/ThreadPool.h"

namespace controllers {
namespace {

// 400 + 参数错误。业务结果由 body 的 code 表达，HTTP 状态码只表达传输层语义
drogon::HttpResponsePtr badRequest(common::ErrCode code,
                                   const std::string& message,
                                   const std::string& requestId) {
    const auto body = models::Result<int>::fail(code, message, requestId);
    auto resp = drogon::HttpResponse::newHttpJsonResponse(body.toJson());
    resp->setStatusCode(drogon::k400BadRequest);
    return resp;
}

// 参数必须存在且为正整数
bool hasPositiveInt(const Json::Value& json, const char* key) {
    return json.isMember(key) && json[key].isNumeric() && json[key].asInt64() > 0;
}

// 请求体 SHA-256，写入幂等表 request_hash，用于识别「同 ID 不同内容」的调用
std::string sha256Hex(const std::string& data) {
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, data.data(), data.size());
    EVP_DigestFinal_ex(ctx, md, &len);
    EVP_MD_CTX_free(ctx);

    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (unsigned int i = 0; i < len; ++i) {
        out.push_back(kHex[md[i] >> 4]);
        out.push_back(kHex[md[i] & 0x0F]);
    }
    return out;
}

}  // namespace

void OrderController::createOrder(
    const drogon::HttpRequestPtr& request,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    const std::string requestId = common::requestIdOf(request);  // 已经过 filter 校验
    auto log = common::Logger::withRequestId(requestId);

    // 参数解析只做存在性与范围，业务校验在 Service 层
    const auto json = request->getJsonObject();
    if (!json || !hasPositiveInt(*json, "productId") ||
        !hasPositiveInt(*json, "quantity") || !hasPositiveInt(*json, "userId")) {
        callback(badRequest(common::ErrCode::kParamInvalid,
                            "productId/quantity/userId 必须为正整数", requestId));
        return;
    }

    models::CreateOrderReq req;
    req.requestId   = requestId;
    req.requestHash = sha256Hex(std::string(request->body()));
    req.productId   = static_cast<std::uint64_t>((*json)["productId"].asInt64());
    req.quantity    = (*json)["quantity"].asInt64();
    req.userId      = static_cast<std::uint64_t>((*json)["userId"].asInt64());

    // handler 跑在 IO 循环线程上，这里只解析参数；
    // 同步事务丢到工作线程池，lambda 只按值捕获，Service 在池线程上按请求创建
    const bool submitted = utils::globalThreadPool().submit(
        [req, requestId, callback = std::move(callback)]() mutable {
            services::OrderService service;

            const services::CreateOrderOutcome outcome = service.createOrder(req);

            drogon::HttpResponsePtr resp;
            if (outcome.kind == services::CreateOrderOutcome::Kind::kCached) {
                // 幂等命中：快照逐字节原样返回，不重新解析
                resp = drogon::HttpResponse::newHttpResponse();
                resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
                resp->setBody(outcome.cachedBody);
            } else {
                resp = drogon::HttpResponse::newHttpJsonResponse(
                    outcome.result.toJson());
            }

            // 响应切回 IO 循环线程再回调
            drogon::app().getLoop()->queueInLoop(
                [resp, callback = std::move(callback)]() { callback(resp); });
        });

    if (!submitted) {
        // 线程池已停止（进程退出中），此处仍在 handler 线程，直接回
        log->warn("thread pool stopped, reject request");
        const auto body = models::Result<int>::fail(
            common::ErrCode::kInternalError, "服务正在关闭，请稍后重试", requestId);
        auto resp = drogon::HttpResponse::newHttpJsonResponse(body.toJson());
        resp->setStatusCode(drogon::k503ServiceUnavailable);
        callback(resp);
    }
}

}  // namespace controllers
