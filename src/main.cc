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
#include "controllers/HealthController.h"

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
    // 2. 把 Drogon / trantor 自己的日志也接到 spdlog 上。
    //    不接的话进程里会有两条互不相干的日志流（trantor 走它的 stdout，
    //    业务走 spdlog），排查问题时得在两个地方对着时间戳找，非常难受。
    //    接了之后全进程只有一条流、一种格式、一份落盘文件。
    //
    //    若这套 API 在你的 trantor 版本上编译不过，整段注释掉即可 ——
    //    只影响日志归并，不影响功能。
    // ------------------------------------------------------------------------
    trantor::Logger::setOutputFunction(
        [](const char* msg, uint64_t len) {
            auto logger = common::Logger::get();
            if (logger) {
                logger->info("[drogon] {}", std::string(msg, static_cast<std::size_t>(len)));
            }
        },
        []() {
            auto logger = common::Logger::get();
            if (logger) {
                logger->flush();
            }
        });

    const std::string configPath = parseConfigPath(argc, argv);

    // ------------------------------------------------------------------------
    // 3. 加载配置。内含 listener / db_clients / redis_clients / app；
    //    自定义的 rabbitmq 段会被 Drogon 归到 customConfig，
    //    用 drogon::app().getCustomConfig()["rabbitmq"] 取。
    // ------------------------------------------------------------------------
    drogon::app().loadConfigFile(configPath);

    // ------------------------------------------------------------------------
    // 4. IO 线程数。
    //    必须在 loadConfigFile **之后**调用，否则会被配置里的值覆盖回去。
    //    这里显式设置，让代码成为最终事实来源而不依赖 config 的键名拼写。
    // ------------------------------------------------------------------------
    drogon::app().setThreadNum(kIoThreads);

    // ------------------------------------------------------------------------
    // 5. Controller 无需手工注册。
    //    HealthController 继承自 drogon::HttpController<T>，其 AutoCreation
    //    默认为 true，对象在静态初始化阶段就自注册好了 —— 这里 include 它的头文件
    //    （进而让 .cc 被链进来）就是注册的全部条件。
    //    详见 HealthController.h 里关于 ADD_METHOD_TO 的说明。
    // ------------------------------------------------------------------------

    log->info("trade_server starting: config={} io_threads={}", configPath, kIoThreads);

    // ------------------------------------------------------------------------
    // 6. 启动事件循环，阻塞至退出。
    // ------------------------------------------------------------------------
    drogon::app().run();

    // ------------------------------------------------------------------------
    // 7. 退出前冲刷日志。
    //    run() 返回后不能再调用 log->xxx —— shutdown() 会释放线程池，
    //    此后的日志调用是未定义行为。所以这条 info 必须在 shutdown 之前。
    // ------------------------------------------------------------------------
    log->info("trade_server stopped");
    common::Logger::shutdown();

    return 0;
}
