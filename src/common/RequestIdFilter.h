// 写接口统一校验 X-Request-Id 的过滤器。
// 用法：ADD_METHOD_TO(..., drogon::Post, "common::RequestIdFilter")
// 注册名必须带命名空间全限定（DrObject 用 demangle 后的名字注册），
// HttpFilter 继承 DrObject，链接即自注册，main.cc 不用管。
#pragma once

#include <drogon/HttpFilter.h>
#include <drogon/HttpRequest.h>

#include <string>

namespace common {

class RequestIdFilter : public drogon::HttpFilter<RequestIdFilter> {
public:
    // 不通过：400 + 错误码 10002，不再进后续过滤链。
    // 通过：rid 写入请求属性，后续 requestIdOf() 取出，全链路同源。
    void doFilter(const drogon::HttpRequestPtr& req,
                  drogon::FilterCallback&& fcb,
                  drogon::FilterChainCallback&& fccb) override;
};

// 校验规则：UUID v4 或 32 位十六进制，长度 16~64，字符 [0-9a-fA-F-]。
bool isValidRequestId(const std::string& rid);

// 取本次请求的 rid：先读过滤器写入的属性；未经过滤器（如 GET）
// 回退读原始请求头，都没有返回空串。
std::string requestIdOf(const drogon::HttpRequestPtr& req);

}  // namespace common
