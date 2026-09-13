// 健康检查接口
// GET /health -> {"code":0,"message":"成功","data":{"status":"up"},"requestId":"..."}
// 供容器健康探针、压测前确认服务就绪用。
#pragma once

#include <functional>

#include <drogon/HttpController.h>

namespace controllers {

class HealthController : public drogon::HttpController<HealthController> {
public:
    METHOD_LIST_BEGIN

    // 用 ADD_METHOD_TO 而非 METHOD_ADD：后者会把类名拼成路径前缀，
    // 实际路径会变成 /HealthController/health；前者接受绝对路径。
    // 控制器也不用在 main 里手工注册，DrObject 静态初始化期自注册。
    ADD_METHOD_TO(HealthController::health, "/health", drogon::Get);

    METHOD_LIST_END

    void health(const drogon::HttpRequestPtr& request,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback);
};

}  // namespace controllers
