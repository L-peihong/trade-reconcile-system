#include "common/RequestIdFilter.h"

#include <string>

#include <jsoncpp/json/json.h>

#include "common/ErrorCode.h"
#include "models/Result.h"

namespace common {
namespace {

// 写入请求属性的键，与 requestIdOf() 共用。
constexpr const char* kRequestIdAttr = "x-request-id";

}  // namespace

bool isValidRequestId(const std::string& rid) {
    if (rid.size() < 16 || rid.size() > 64) {
        return false;
    }
    for (const char c : rid) {
        const bool hex = (c >= '0' && c <= '9') ||
                         (c >= 'a' && c <= 'f') ||
                         (c >= 'A' && c <= 'F');
        if (!hex && c != '-') {
            return false;
        }
    }
    return true;
}

void RequestIdFilter::doFilter(const drogon::HttpRequestPtr& req,
                               drogon::FilterCallback&& fcb,
                               drogon::FilterChainCallback&& fccb) {
    const auto method = req->method();
    // GET / HEAD / OPTIONS 不强制（CLAUDE.md 4.2），直接放行
    if (method == drogon::Get || method == drogon::Head ||
        method == drogon::Options) {
        fccb();
        return;
    }

    const std::string rid = req->getHeader("X-Request-Id");
    if (isValidRequestId(rid)) {
        // 写入请求属性，controller 用 requestIdOf() 取出 —— 日志、幂等键、
        // 消息头全程同一份值（Attributes::insert 接口见 drogon Attribute.h）。
        req->attributes()->insert(kRequestIdAttr, rid);
        fccb();
        return;
    }

    const auto body = models::Result<int>::fail(
        ErrCode::kMissingRequestId,
        toMessage(ErrCode::kMissingRequestId),
        rid);
    auto resp = drogon::HttpResponse::newHttpJsonResponse(body.toJson());
    resp->setStatusCode(drogon::k400BadRequest);
    fcb(resp);
}

std::string requestIdOf(const drogon::HttpRequestPtr& req) {
    if (!req) {
        return {};
    }
    if (req->attributes()->find(kRequestIdAttr)) {
        try {
            return req->attributes()->get<std::string>(kRequestIdAttr);
        } catch (...) {
            // 类型不符（理论不可达）→ 降级读原始请求头
        }
    }
    return req->getHeader("X-Request-Id");
}

}  // namespace common
