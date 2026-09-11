// ============================================================================
// 日志封装（CLAUDE.md 4.4）
//
// 全进程只走这一个日志入口，禁止 std::cout / printf。
//
// 输出格式（request_id 列为固定宽度，便于 awk/grep 按列切）：
//   [2026-09-11 15:30:00.123][info][-] 服务启动中          ← 无请求上下文
//   [2026-09-11 15:30:00.456][info][3f2a9c1e-...] 下单成功  ← 请求上下文内
//
// 异步：所有日志投递到 spdlog 的 thread_pool，业务线程不被磁盘 IO 阻塞。
// ============================================================================
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
    // 进程启动时调用一次。可重复调用，只有首次真正建 sink / 线程池；
    // 后续调用只调整日志级别。
    static void init(spdlog::level::level_enum level = spdlog::level::debug);

    // 全局 logger，不带请求上下文。
    // 内部会保证 init 已执行，所以任何地方直接调用都安全。
    static std::shared_ptr<spdlog::logger> get();

    // 取一个绑定了 request_id 的 logger。
    //
    // 实现方式：新建一个 async_logger，与全局 logger **共享同一批 sink 和同一个
    // 线程池**，只是把 request_id 作为字面量编进了 pattern —— 于是每条日志的
    // [request_id] 列自动带上它，业务代码不用在每条日志里手写 rid。
    //
    // requestId 非法或为空时退回全局 logger（不含 rid 列，值显示为 "-"）。
    //
    // 开销：每个请求一次 logger + pattern 编译，量级是一次小内存分配。相对一次
    // 数据库往返可以忽略。若将来 QPS 高到成为瓶颈，再考虑按 rid 分桶复用。
    static std::shared_ptr<spdlog::logger> withRequestId(const std::string& requestId);

    // 冲刷队列并释放资源。进程退出前必须调用，否则队列里未落盘的日志会丢。
    static void shutdown();

    // 校验 request_id 是否合法，合法则原样返回，否则返回空串。
    //
    // 为什么必须校验（两件事，都是安全问题）：
    //   1. **日志注入**：rid 直接来自 HTTP 头。若放行 \n，攻击者可以伪造出
    //      一整套看起来完全正常的日志行，污染审计记录 —— 对账和事故复盘时
    //      这是致命的。
    //   2. **pattern 注入**：rid 是要拼进 spdlog pattern 的。若放行 %，
    //      例如 rid = "%Y"，pattern 会被改写，行为不可预期。
    // 只放行 [A-Za-z0-9_-] 且长度 <= 64，两条路一次堵死。
    static std::string sanitizeRequestId(const std::string& raw);

    Logger() = delete;  // 纯静态工具类，不允许实例化

private:
    static void doInit(spdlog::level::level_enum level);
    static void ensureInitialized();

    // 注意：这些是**静态成员**，定义在 Logger.cc 中。
    static std::shared_ptr<spdlog::async_logger>          logger_;
    static std::shared_ptr<spdlog::details::thread_pool>  pool_;
    static std::vector<spdlog::sink_ptr>                  sinks_;
    // shutdown() 后为 true：ensureInitialized 提前返回、get() 返回空指针，
    // 调用方必须判空（ThreadPool 的兜底日志依赖此语义）。
    static std::atomic<bool> shutdownFlag_;
};

}  // namespace common
