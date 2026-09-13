// 进程入口
// 运行：./build/debug/trade_server（需在仓库根目录启动，配置是相对路径）
//       ./build/debug/trade_server --config ./config/config.json
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
    // 1. 日志最先初始化，放在 Drogon 之前 —— 初始化失败也能留痕
    common::Logger::init(spdlog::level::debug);
    auto log = common::Logger::get();

    // 2. 把 Drogon / trantor 日志接到 spdlog，全进程一条日志流。
    //    若 trantor 版本不兼容，整段注释掉即可，只影响日志归并
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

    // 3. 预热工作线程池：同步事务统一跑在池线程，
    //    事件循环线程上禁止调用同步 DB 接口（会死锁）
    utils::globalThreadPool();

    // 4. Controller / Filter 无需手工注册：DrObject<T> 静态初始化期自注册，
    //    注册名是全限定名（路由宏里引用过滤器必须写全限定名）。
    //    前提是目标文件被链进来 —— trade_core 是静态库，链接时用
    //    --whole-archive 强制纳入，否则无人引用的注册对象会被链接器丢弃，
    //    表现为编译全过、运行时路由 404。

    const std::string configPath = parseConfigPath(argc, argv);

    // 5. 加载配置。自定义的 rabbitmq 段归到 customConfig，
    //    用 getCustomConfig()["rabbitmq"] 取。
    drogon::app().loadConfigFile(configPath);

    // 6. IO 线程数，必须在 loadConfigFile 之后设置，否则被配置里的值覆盖
    drogon::app().setThreadNum(kIoThreads);

    // 7. 定时任务：本地消息表投递。
    //    定时器回调只做防重入 + 提交线程池，实际投递在池线程。
    tasks::MessageRelayTask::instance().start();

    // 8. MQ 消费者：下游通知（手动 ACK + 幂等 + 死信）。
    //    独立常驻线程跑阻塞式消费循环。
    consumer::NotifyConsumer::instance().start();

    // 9. 日终对账定时任务：每分钟检查账单文件与当日批次，就绪则跑批。
    tasks::ReconcileTask::instance().start();

    log->info("trade_server starting: config={} io_threads={} pool_threads={}",
              configPath, kIoThreads, utils::globalThreadPool().threadCount());

    // 10. 启动事件循环，阻塞至退出。
    drogon::app().run();

    // 11. 退出前冲刷日志：shutdown() 会释放线程池，之后的日志调用返回空指针，
    //     这条 info 必须写在 shutdown 之前。
    log->info("trade_server stopped");
    common::Logger::shutdown();

    return 0;
}
