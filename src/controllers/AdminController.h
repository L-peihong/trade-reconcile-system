// ============================================================================
// 管理接口（链路⑤）：跑批触发 / 差异查询 / 差异核销
// handler 只解析参数并 submit 到工作线程池（CLAUDE.md 4.1/6.6）。
// ============================================================================
#pragma once

#include <cstdint>

#include <drogon/HttpController.h>

#include <functional>

namespace controllers {

class AdminController : public drogon::HttpController<AdminController> {
public:
    METHOD_LIST_BEGIN
    // 手动触发对账
    ADD_METHOD_TO(AdminController::runReconcile, "/api/v1/reconcile/run",
                  drogon::Post, "common::RequestIdFilter");
    // 差异查询（GET 不强制 X-Request-Id，CLAUDE.md 4.2）
    ADD_METHOD_TO(AdminController::listDiffs, "/api/v1/reconcile/diffs",
                  drogon::Get);
    // 差异核销/忽略
    ADD_METHOD_TO(AdminController::handleDiff,
                  "/api/v1/reconcile/diffs/{1}/handle",
                  drogon::Post, "common::RequestIdFilter");
    METHOD_LIST_END

    void runReconcile(const drogon::HttpRequestPtr& request,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    void listDiffs(const drogon::HttpRequestPtr& request,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    void handleDiff(const drogon::HttpRequestPtr& request,
                    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    std::int64_t diffId);
};

}  // namespace controllers
