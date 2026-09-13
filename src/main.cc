// ============================================================================
// 高可靠交易与对账系统 —— 进程入口
//
// 构建：cmake --preset debug && cmake --build build/debug -j$(nproc)
// 运行：./build/debug/trade_server                （在仓库根目录执行）
//       ./build/debug/trade_server --config ./config/config.json
//
// ⚠️ 必须在仓库根目录启动：config/config.json 是相对路径。
// ============================================================================
#include <drogon/drogon.h>
#include <spdlog/spdlog.h>
#include <trantor/utils/Logger.h>

#include <cstdint>
#include <string>

#include "common/Logger.h"
#include "common/RequestIdFilter.h"
#include "consumer/NotifyConsumer.h"
#include "controllers/HealthController.h"
#include "controllers/OrderController.h"
#include "tasks/MessageRelayTask.h"
#include "tasks/ReconcileTask.h"
#include "utils/ThreadPool.h"

namespace {

// 解析 --config <path>，未指定则用默认路径。
std::string parseConfigPath(int argc, char* argv[]) {
    std::string path = "./config/config.json";
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--config") {
            path = argv[i + 1];
            ++i;  // 跳过值，避免把路径本身当成下一个选项
        }
    }
    return path;
}

constexpr int kIoThreads = 4;

}  // namespace

int main(int argc, char* argv[]) {
    // ------------------------------------------------------------------------
    // 1. 日志系统最先起来，后续每一步都能留下痕迹。
    //    放在 Drogon 之前是有意的：Drogon 初始化失败时我们要能记录原因。
    // ------------------------------------------------------------------------
    common::Logger::init(spdlog::level::debug);
    auto log = common::Logger::get();

    // ------------------------------------------------------------------------
    // 2. 把 Drogon / trantor 自己的日志也接到 spdlog 上（全进程一条日志流）。
    //    若这套 API 在你的 trantor 版本上编译不过，整段注释掉即可 ——
    //    只影响日志归并，不影响功能。
    // ------------------------------------------------------------------------
    trantor::Logger::setOutputFunction(
        [](const char* msg, uint64_t len) {
            auto logger = common::Logger::get();
            if (logger) {
                logger->info("[drogon] {}",
                             std::string(msg, static_cast<std::size_t>(len)));
            }
        },
        []() {
            auto logger = common::Logger::get();
            if (logger) {
                logger->flush();
            }
        });

    // ------------------------------------------------------------------------
    // 3. 预热工作线程池（CLAUDE.md 4.1/6.6）：同步事务统一跑在这里，
    //    Drogon 事件循环线程禁止调用同步 DB API（死锁/assert）。
    //    静态局部变量初始化本身线程安全，显式调用让「池在服务开始前已就绪」
    //    成为确定事实。
    // ------------------------------------------------------------------------
    utils::globalThreadPool();

    // ------------------------------------------------------------------------
    // 4. Controller / Filter 无需手工注册：
    //    - HttpController<T> 与 HttpFilter<T> 都继承 DrObject<T>，静态初始化期
    //      自注册；注册名 = demangle(typeid(T).name()) 的**全限定名**
    //      （如 "controllers::OrderController"、"common::RequestIdFilter"）——
    //      路由宏里引用过滤器必须用全限定名。
    //    - 前提是目标文件被链接进来：trade_core 是静态库，CMakeLists 已用
    //      -Wl,--whole-archive 强制纳入，否则无人引用的注册对象会被链接器
    //      丢弃 → 编译全过、运行时路由 404（CLAUDE.md 3.2）。
    //    这里的 include 仅作显式声明，实际注册由上述机制完成。
    // ------------------------------------------------------------------------

    const std::string configPath = parseConfigPath(argc, argv);

    // ------------------------------------------------------------------------
    // 5. 加载配置。内含 listener / db_clients / redis_clients / app；
    //    自定义的 rabbitmq 段会被 Drogon 归到 customConfig，
    //    用 drogon::app().getCustomConfig()["rabbitmq"] 取。
    // ------------------------------------------------------------------------
    drogon::app().loadConfigFile(configPath);

    // ------------------------------------------------------------------------
    // 6. IO 线程数。
    //    必须在 loadConfigFile **之后**调用，否则会被配置里的值覆盖回去。
    // ------------------------------------------------------------------------
    drogon::app().setThreadNum(kIoThreads);

    // ------------------------------------------------------------------------
    // 7. 定时任务：本地消息表投递（链路④，CLAUDE.md 3.6 里程碑 4）。
    //    定时器回调只做防重入+提交线程池，实际投递在池线程（6.6/6.8）。
    // ------------------------------------------------------------------------
    tasks::MessageRelayTask::instance().start();

    // ------------------------------------------------------------------------
    // 8. MQ 消费者：下游通知（手动 ACK + 幂等 + 死信，CLAUDE.md 6.2）。
    //    独立常驻线程,阻塞式消费循环 —— 不在事件循环上。
    // ------------------------------------------------------------------------
    consumer::NotifyConsumer::instance().start();

    // ------------------------------------------------------------------------
    // 9. 日终对账定时任务（链路⑤，CLAUDE.md 3.6 里程碑 5）：
    //    每分钟检查账单文件与当日批次,就绪则跑批(幂等)。
    // ------------------------------------------------------------------------
    tasks::ReconcileTask::instance().start();

    log->info("trade_server starting: config={} io_threads={} pool_threads={}",
              configPath, kIoThreads, utils::globalThreadPool().threadCount());

    // ------------------------------------------------------------------------
    // 7. 启动事件循环，阻塞至退出。
    // ------------------------------------------------------------------------
    drogon::app().run();

    // ------------------------------------------------------------------------
    // 8. 退出前冲刷日志。
    //    run() 返回后不能再调用 log->xxx —— shutdown() 会释放线程池，
    //    此后的日志调用返回空指针。所以这条 info 必须在 shutdown 之前。
    // ------------------------------------------------------------------------
    log->info("trade_server stopped");
    common::Logger::shutdown();

    return 0;
}
