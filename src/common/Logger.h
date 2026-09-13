// 全项目唯一的日志入口，spdlog 异步封装，禁止直接用 cout/printf。
// 格式：[2026-09-11 15:30:00.123][info][rid] msg，rid 列为固定宽度方便 awk 切列。
// 无请求上下文时 rid 显示 "-"。
#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include <spdlog/async_logger.h>
#include <spdlog/details/thread_pool.h>
#include <spdlog/spdlog.h>

namespace common {

class Logger {
public:
    // 启动时调用一次。可重复调用：首次建 sink/线程池，之后只调级别。
    static void init(spdlog::level::level_enum level = spdlog::level::debug);

    // 全局 logger，不带请求上下文，内部保证已初始化。
    static std::shared_ptr<spdlog::logger> get();

    // 取绑定 request_id 的 logger。实现上新建 async_logger，与全局 logger
    // 共享同一批 sink 和线程池，只把 rid 编进 pattern，日志行自动带出。
    // rid 非法或为空时退回全局 logger。
    // 每个请求一次小内存分配，相对 DB 往返可忽略；真成瓶颈再按 rid 分桶复用。
    static std::shared_ptr<spdlog::logger> withRequestId(const std::string& requestId);

    // 冲刷队列并释放资源。退出前调用，否则没落盘的日志会丢。
    static void shutdown();

    // 校验 rid，合法原样返回，非法返回空串。
    // 两个安全点：rid 来自 HTTP 头，放行 \n 能伪造整行日志污染审计；
    // rid 会拼进 spdlog pattern，放行 % 会改写 pattern。
    // 所以白名单只放 [A-Za-z0-9_-]，长度 <= 64。
    static std::string sanitizeRequestId(const std::string& raw);

    Logger() = delete;  // 纯静态工具类

private:
    static void doInit(spdlog::level::level_enum level);
    static void ensureInitialized();

    // 静态成员，定义在 Logger.cc
    static std::shared_ptr<spdlog::async_logger>          logger_;
    static std::shared_ptr<spdlog::details::thread_pool>  pool_;
    static std::vector<spdlog::sink_ptr>                  sinks_;
    // shutdown() 后为 true，get() 返回空指针，调用方要判空
    static std::atomic<bool> shutdownFlag_;
};

}  // namespace common
