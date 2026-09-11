// ============================================================================
// 健康检查接口
//
// GET /health -> {"code":0,"message":"成功","data":{"status":"up"},"requestId":"..."}
//
// 用途：容器健康探针、K8s liveness/readiness、压测前确认服务已就绪。
// ============================================================================
#pragma once

#include <functional>

#include <drogon/HttpController.h>

namespace controllers {

class HealthController : public drogon::HttpController<HealthController> {
public:
    METHOD_LIST_BEGIN

    // ⚠️ 这里用 ADD_METHOD_TO 而不是 METHOD_ADD，不是风格偏好，是路径语义的差别：
    //
    //   METHOD_ADD(HealthController::health, "/health", Get)
    //       → 实际路径 /HealthController/health
    //         因为 HttpController<T> 会自动把**类名**作为前缀拼在相对路径前面。
    //         想让 METHOD_ADD 生效就得写成 /HealthController/health，
    //         而按 CLAUDE.md 的接口约定我们要的是 /health。
    //
    //   ADD_METHOD_TO(HealthController::health, "/health", Get)
    //       → 实际路径 /health
    //         它接受绝对路径，不做类名前缀拼接。
    //
    // 顺带一提：这个类**不需要**在 main.cc 里手工 registerController。
    // HttpController<T> 的第二个模板参数 AutoCreation 默认为 true，
    // 对象在静态初始化阶段就自注册了 —— 只要 HealthController.cc 被链进可执行
    // 文件即可。手工再注册一次反而会撞上重复路径。
    ADD_METHOD_TO(HealthController::health, "/health", drogon::Get);

    METHOD_LIST_END

    void health(const drogon::HttpRequestPtr& request,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback);
};

}  // namespace controllers
