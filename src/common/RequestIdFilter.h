// ============================================================================
// 写接口强制 X-Request-Id（CLAUDE.md 4.2）
//
// 用法：挂到写路由上 —— ADD_METHOD_TO(..., drogon::Post, "common::RequestIdFilter")
// ⚠️ 名字必须带命名空间全限定：DrObject<T> 的注册名是
//    DrClassMap::demangle(typeid(T).name())，即 "common::RequestIdFilter"，
//    不带命名空间的短名匹配不到（DrObject.h:119-121）。
//
// 无需在 main.cc 手工注册：HttpFilter<T> 继承 DrObject<T>，链接进可执行
// 文件即完成静态自注册（与 HttpController<T> 同一机制）。
// ============================================================================
#pragma once

#include <drogon/HttpFilter.h>
#include <drogon/HttpRequest.h>

#include <string>

namespace common {

class RequestIdFilter : public drogon::HttpFilter<RequestIdFilter> {
public:
    // 校验不通过：返回 400 + 错误码 10002，不再进入后续过滤链。
    // 校验通过：把 request_id 写入请求属性（HttpRequest::attributes()），
    // 后续用 requestIdOf() 取出，全链路同源。
    void doFilter(const drogon::HttpRequestPtr& req,
                  drogon::FilterCallback&& fcb,
                  drogon::FilterChainCallback&& fccb) override;
};

// 校验规则（4.2）：UUID v4 或 32 位十六进制，长度 16~64，字符 [0-9a-fA-F-]。
bool isValidRequestId(const std::string& rid);

// 取本次请求的 request_id：过滤链由 RequestIdFilter 写入属性；
// 未经过滤器（如 GET）时回退读原始请求头，未携带返回空串。
std::string requestIdOf(const drogon::HttpRequestPtr& req);

}  // namespace common
