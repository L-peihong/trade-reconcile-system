// ============================================================================
// 下游通知消费者（链路④消费侧，CLAUDE.md 4.6/5.8/6.2）
//
// 语义：
//   - 手动 ACK（autoAck=false）：处理成功才 basic_ack，失败 basic_nack(requeue=false)
//     转死信队列 —— 绝不无限重投（CLAUDE.md 6.2）；
//   - 幂等：t_notify_record.uk_message_id 去重，重复投递（至少一次语义的必然产物）
//     直接 ACK 跳过；
//   - 消费者内指数退避 2 次（1s/2s），仍失败 → nack → 死信，人工介入（V1.5 的死信消费者）。
//
// 线程模型：独立常驻线程（阻塞式 rabbitmq-c 消费循环）。DB 为同步调用、
// HTTP 通知经 drogon HttpClient 同步等待 —— 均不在事件循环线程上（6.6）。
// ⚠️ V1 不做优雅退出：detach 线程，随进程终止（记录在案）。
// ============================================================================
#pragma once

#include <string>

#include <amqp.h>

namespace consumer {

class NotifyConsumer {
public:
    static NotifyConsumer& instance();

    // 启动后台消费线程（main.cc 调用一次）
    void start();

private:
    NotifyConsumer() = default;

    void runLoop();                 // 常驻线程：连接/订阅 → 消费 → 断线重连
    bool connectAndSubscribe();
    void consumeLoop();             // 阻塞消费，连接断返回
    void handleMessage(const amqp_envelope_t& envelope);
    bool process(const std::string& body);  // 幂等落库 → HTTP 通知 → 判定 ack/nack

    // 仅消费线程访问
    amqp_connection_state_t conn_ = nullptr;
    std::string notifyBaseUrl_ = "http://localhost:8080";
};

}  // namespace consumer
