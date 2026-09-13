// 本地消息表 → RabbitMQ 投递任务（消息与业务写同事务落库，本任务负责异步投递）
// 定时器回调（事件循环线程）只做防重入检查并提交线程池，
// 扫描 DB 与 MQ 投递都在工作线程池执行。
// 防重入：单实例用原子标志；多实例部署需要 Redis 分布式锁。
#pragma once

#include <atomic>
#include <memory>

namespace utils {
class MqClient;
}

namespace dao {
class LocalMessageDao;
}

namespace tasks {

class MessageRelayTask {
public:
    static MessageRelayTask& instance();

    // 注册定时器（每 5 秒）。由 main.cc 在配置加载后调用一次。
    void start();

private:
    MessageRelayTask() = default;

    void runOnce();  // 定时器回调（事件循环线程）：防重入 → 提交线程池
    void relay();    // 池线程：扫描 → 投递 → 更新状态

    // 仅在 relay()（单线程串行）中访问，无数据竞争
    std::unique_ptr<utils::MqClient> mqClient_;
    std::atomic<bool> running_{false};
};

}  // namespace tasks
