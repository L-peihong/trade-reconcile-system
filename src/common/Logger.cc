#include "common/Logger.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <utility>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace common {

namespace {

// ---------------- 可调参数 ----------------

// 无请求上下文时的 pattern。[-] 是 request_id 列的占位符，
// 保持与请求日志同列宽，grep / awk 按列切时不会错位。
constexpr const char* kBasePattern = "[%Y-%m-%d %H:%M:%S.%e][%l][-] %v";
constexpr const char* kLoggerName  = "trade";

constexpr const char* kLogDir      = "logs";
constexpr const char* kLogFile     = "logs/trade_server.log";

// 异步队列长度。队列满时采取阻塞策略，见 doInit 里的说明。
constexpr std::size_t kQueueSize = 8192;

// 单文件 64MB × 5 个滚动，单机最多占 320MB，够 7 天 MVP 用。
constexpr std::size_t kMaxFileSize = 64 * 1024 * 1024;
constexpr std::size_t kMaxFiles    = 5;

// request_id 允许的最大长度，与 CLAUDE.md 4.2 的 16–64 对齐。
constexpr std::size_t kMaxRequestIdLen = 64;

}  // namespace

// ------------------------- 静态成员定义 -------------------------
std::shared_ptr<spdlog::async_logger>         Logger::logger_;
std::shared_ptr<spdlog::details::thread_pool> Logger::pool_;
std::vector<spdlog::sink_ptr>                 Logger::sinks_;
std::atomic<bool>                             Logger::shutdownFlag_{false};

// ------------------------------- init -------------------------------

void Logger::init(spdlog::level::level_enum level) {
    // 函数内静态变量：整个进程只有一个 once_flag，
    // 保证 doInit 无论从哪条路径进来都只执行一次。
    static std::once_flag initFlag;
    std::call_once(initFlag, [level]() { doInit(level); });

    // 级别每次都设置 —— 这样"先 get() 隐式初始化，后 init(info)"和
    // "先 init(info)"两种顺序行为一致，都是最后一次调用说了算。
    if (logger_) {
        logger_->set_level(level);
    }
    spdlog::set_level(level);
}

void Logger::ensureInitialized() {
    // shutdown 之后不再初始化、不碰 spdlog 全局注册表 —— 它已随
    // spdlog::shutdown() 释放，再碰是未定义行为。此后的 get() 返回空指针，
    // 调用方判空即可安全降级。
    if (shutdownFlag_.load(std::memory_order_acquire)) {
        return;
    }
    // 直接复用 init 的 call_once：首次进来会用默认级别建好，
    // 非首次是空操作。关键是这里**不改级别** —— 否则每次 get() 都会把
    // 调用方刚设好的级别打回默认值。
    init(spdlog::level::debug);
}

// ------------------------------- get --------------------------------

std::shared_ptr<spdlog::logger> Logger::get() {
    ensureInitialized();
    return logger_;
}

// -------------------------- withRequestId ---------------------------

std::shared_ptr<spdlog::logger> Logger::withRequestId(const std::string& requestId) {
    ensureInitialized();

    const std::string safe = sanitizeRequestId(requestId);
    if (safe.empty()) {
        // request_id 缺失或非法。这里不抛异常 —— 日志本身不该成为业务失败的原因。
        // 正常情况下 RequestIdFilter 已经在入口拦掉了非法值，走到这里说明有
        // 代码路径绕过了 filter，降级记录一下便于发现。
        if (!requestId.empty()) {
            logger_->warn("illegal X-Request-Id rejected, falling back to global logger");
        }
        return logger_;
    }

    // 与全局 logger 共享 sinks 与 thread pool，仅 pattern 不同。
    // 复用 pool 是关键：新建线程池会导致日志乱序，且线程数随请求数增长。
    auto reqLogger = std::make_shared<spdlog::async_logger>(
        std::string(kLoggerName) + "." + safe,
        sinks_.begin(),
        sinks_.end(),
        pool_,
        spdlog::async_overflow_policy::block);

    // request_id 作为**字面量**编进 pattern。
    // safe 已经过白名单过滤（只含 [A-Za-z0-9_-]），不可能出现 % 把 pattern 改写掉。
    reqLogger->set_pattern("[%Y-%m-%d %H:%M:%S.%e][%l][" + safe + "] %v");
    reqLogger->set_level(logger_->level());
    reqLogger->flush_on(spdlog::level::err);

    return reqLogger;
}

// -------------------------- sanitizeRequestId -----------------------

std::string Logger::sanitizeRequestId(const std::string& raw) {
    if (raw.empty() || raw.size() > kMaxRequestIdLen) {
        return {};
    }

    std::string out;
    out.reserve(raw.size());

    for (const char c : raw) {
        const bool allowed =
            (c >= '0' && c <= '9') ||
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            c == '-' || c == '_';

        if (!allowed) {
            // 遇到非法字符**整体作废**，而不是过滤掉继续用。
            // 理由：形如 "abc\nfake" 的输入若被截成 "abcfake"，会和真实 rid
            // 混淆；直接判非法则退化成无上下文日志，异常一眼可见。
            return {};
        }
        out.push_back(c);
    }
    return out;
}

// ------------------------------ shutdown ----------------------------

void Logger::shutdown() {
    // 先立旗再清理：在途线程若此刻正在 ensureInitialized/init，
    // 之后的调用会拿到空 logger，而不是碰已释放的 spdlog 注册表。
    shutdownFlag_.store(true, std::memory_order_release);

    if (logger_) {
        // async_logger::flush() 会把一个 flush 任务投进线程池的 FIFO 队列并
        // 等待它完成 —— 队列是先进先出的，所以这一步等价于"排空前面积压的所有
        // 日志"。不调用它直接析构 logger，队列里没落盘的消息会整批丢掉。
        logger_->flush();
    }

    // 停掉 flush_every 的后台线程并清空 registry。
    // 必须在 logger_.reset() 之前调用，否则 registry 里还留着指向已析构对象的
    // 弱引用，后台线程下一轮 flush 会踩空指针。
    spdlog::shutdown();

    logger_.reset();
    pool_.reset();
    sinks_.clear();
}

// ------------------------------- doInit -----------------------------

void Logger::doInit(spdlog::level::level_enum level) {
    // spdlog 的 rotating_file_sink 在父目录不存在时会抛 spdlog_ex，
    // 这里显式建好，不依赖它的实现细节。
    std::error_code ec;
    std::filesystem::create_directories(kLogDir, ec);

    std::vector<spdlog::sink_ptr> sinks;

    // 控制台 sink：带颜色，方便本地开发肉眼扫
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    consoleSink->set_level(spdlog::level::trace);  // 逐 sink 不过滤，统一由 logger 级别管
    sinks.push_back(consoleSink);

    // 文件 sink：滚动 + 落盘，作为对账与事故复盘的依据
    try {
        auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            kLogFile, kMaxFileSize, kMaxFiles, /*rotate_on_open=*/false);
        fileSink->set_level(spdlog::level::trace);
        sinks.push_back(fileSink);
    } catch (const spdlog::spdlog_ex& e) {
        // 文件 sink 建不起来（目录只读、磁盘满等）时不阻断启动，降级为仅控制台。
        // 这里只能走 std::cerr —— 我们的 logger 正是在这一步才被创建出来，
        // 鸡生蛋问题，属于 logger 初始化路径上唯一合理的 cout 例外。
        std::cerr << "[Logger] WARN: file sink unavailable, console-only logging. reason: "
                  << e.what() << std::endl;
    }

    // 线程池：(队列长度, 后台线程数)。
    // 1 个后台线程足够 —— 日志写入是顺序 IO，多线程只会引入锁竞争。
    pool_ = std::make_shared<spdlog::details::thread_pool>(kQueueSize, 1);

    // 溢出策略选 block（阻塞）而不是 overrun_oldest（丢弃最旧）：
    //   本系统的日志是对账与事故复盘的依据，丢一条日志的代价可能高于一次请求
    //   多等几毫秒。队列 8192 条 + 单线程顺序写，正常负载下永远摸不到上限。
    //   若将来发现日志确实拖慢了交易线程，再改成 overrun_oldest，
    //   同时必须接上 pool_->overrun_counter() 做监控告警。
    logger_ = std::make_shared<spdlog::async_logger>(
        kLoggerName,
        sinks.begin(),
        sinks.end(),
        pool_,
        spdlog::async_overflow_policy::block);

    logger_->set_pattern(kBasePattern);
    logger_->set_level(level);
    // error 及以上立即冲刷，保证崩溃前的错误日志一定在磁盘上
    logger_->flush_on(spdlog::level::err);

    sinks_ = std::move(sinks);

    // 注册进 registry，否则周期冲刷线程遍历不到它
    spdlog::register_logger(logger_);
    spdlog::set_default_logger(logger_);
    spdlog::set_level(level);

    // 每 3 秒冲刷一次，限制断电/被 kill 时最多丢 3 秒日志
    spdlog::flush_every(std::chrono::seconds(3));
}

}  // namespace common
