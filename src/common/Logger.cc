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

// 可调参数

// 无请求上下文时的 pattern，[-] 占 request_id 列位，保证按列切不错位
constexpr const char* kBasePattern = "[%Y-%m-%d %H:%M:%S.%e][%l][-] %v";
constexpr const char* kLoggerName  = "trade";

constexpr const char* kLogDir      = "logs";
constexpr const char* kLogFile     = "logs/trade_server.log";

// 异步队列长度，满时走阻塞策略，见 doInit
constexpr std::size_t kQueueSize = 8192;

// 单文件 64MB × 5 个滚动，最多占 320MB
constexpr std::size_t kMaxFileSize = 64 * 1024 * 1024;
constexpr std::size_t kMaxFiles    = 5;

// request_id 最大长度，与校验规则的 16–64 对齐
constexpr std::size_t kMaxRequestIdLen = 64;

}  // namespace

// 静态成员定义
std::shared_ptr<spdlog::async_logger>         Logger::logger_;
std::shared_ptr<spdlog::details::thread_pool> Logger::pool_;
std::vector<spdlog::sink_ptr>                 Logger::sinks_;
std::atomic<bool>                             Logger::shutdownFlag_{false};

// init

void Logger::init(spdlog::level::level_enum level) {
    // 进程内只有一个 once_flag，doInit 只执行一次
    static std::once_flag initFlag;
    std::call_once(initFlag, [level]() { doInit(level); });

    // 级别每次都设置，先 get 后 init 和先 init 的行为一致
    if (logger_) {
        logger_->set_level(level);
    }
    spdlog::set_level(level);
}

void Logger::ensureInitialized() {
    // shutdown 之后 spdlog 全局注册表已释放，不能再碰，直接返回空 logger
    if (shutdownFlag_.load(std::memory_order_acquire)) {
        return;
    }
    // 复用 init 的 call_once；这里不改级别，不然每次 get 都把级别打回默认值
    init(spdlog::level::debug);
}

// get

std::shared_ptr<spdlog::logger> Logger::get() {
    ensureInitialized();
    return logger_;
}

// withRequestId

std::shared_ptr<spdlog::logger> Logger::withRequestId(const std::string& requestId) {
    ensureInitialized();

    const std::string safe = sanitizeRequestId(requestId);
    if (safe.empty()) {
        // 缺失或非法。日志不该成为业务失败的原因，降级退回全局 logger。
        // 入口 RequestIdFilter 一般已拦掉非法值，走到这里说明有路径绕过了 filter。
        if (!requestId.empty()) {
            logger_->warn("illegal X-Request-Id rejected, falling back to global logger");
        }
        return logger_;
    }

    // 与全局 logger 共享 sinks 和线程池，只有 pattern 不同。
    // 必须复用 pool：新建线程池会打乱日志顺序，线程数还会随请求数涨。
    auto reqLogger = std::make_shared<spdlog::async_logger>(
        std::string(kLoggerName) + "." + safe,
        sinks_.begin(),
        sinks_.end(),
        pool_,
        spdlog::async_overflow_policy::block);

    // rid 作为字面量编进 pattern；safe 已过白名单，不会带 % 改写 pattern
    reqLogger->set_pattern("[%Y-%m-%d %H:%M:%S.%e][%l][" + safe + "] %v");
    reqLogger->set_level(logger_->level());
    reqLogger->flush_on(spdlog::level::err);

    return reqLogger;
}

// sanitizeRequestId

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
            // 遇到非法字符整体作废而不是过滤掉继续用，不然 "abc\nfake"
            // 会被截成 "abcfake"，和真实 rid 混在一起
            return {};
        }
        out.push_back(c);
    }
    return out;
}

// shutdown

void Logger::shutdown() {
    // 先立旗再清理，在途线程后续调用会拿到空 logger，不会碰已释放的注册表
    shutdownFlag_.store(true, std::memory_order_release);

    if (logger_) {
        // flush 任务进 FIFO 队列并等它跑完，等于排空前面所有积压日志
        logger_->flush();
    }

    // 停 flush_every 后台线程并清 registry。必须在 reset 之前调，
    // 不然 registry 里留着指向已析构对象的弱引用
    spdlog::shutdown();

    logger_.reset();
    pool_.reset();
    sinks_.clear();
}

// doInit

void Logger::doInit(spdlog::level::level_enum level) {
    // rotating_file_sink 父目录不存在会抛异常，先建好目录
    std::error_code ec;
    std::filesystem::create_directories(kLogDir, ec);

    std::vector<spdlog::sink_ptr> sinks;

    // 控制台 sink，本地开发肉眼扫
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    consoleSink->set_level(spdlog::level::trace);  // sink 不过滤，统一由 logger 级别管
    sinks.push_back(consoleSink);

    // 文件 sink，滚动落盘留档
    try {
        auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            kLogFile, kMaxFileSize, kMaxFiles, /*rotate_on_open=*/false);
        fileSink->set_level(spdlog::level::trace);
        sinks.push_back(fileSink);
    } catch (const spdlog::spdlog_ex& e) {
        // 文件 sink 建不起来（只读目录、磁盘满等）降级为仅控制台。
        // logger 本身还在初始化中，这里只能走 std::cerr
        std::cerr << "[Logger] WARN: file sink unavailable, console-only logging. reason: "
                  << e.what() << std::endl;
    }

    // 1 个后台线程够用，日志是顺序 IO，多线程只有锁竞争
    pool_ = std::make_shared<spdlog::details::thread_pool>(kQueueSize, 1);

    // 溢出策略用 block 不丢日志：日志是对账和事故复盘的依据，
    // 请求多等几毫秒值得。真拖慢交易线程了再换 overrun_oldest + 告警
    logger_ = std::make_shared<spdlog::async_logger>(
        kLoggerName,
        sinks.begin(),
        sinks.end(),
        pool_,
        spdlog::async_overflow_policy::block);

    logger_->set_pattern(kBasePattern);
    logger_->set_level(level);
    // error 及以上立即落盘，崩溃也不丢
    logger_->flush_on(spdlog::level::err);

    sinks_ = std::move(sinks);

    // 注册进 registry，周期冲刷线程才看得到它
    spdlog::register_logger(logger_);
    spdlog::set_default_logger(logger_);
    spdlog::set_level(level);

    // 3 秒一刷，掉电最多丢 3 秒日志
    spdlog::flush_every(std::chrono::seconds(3));
}

}  // namespace common
