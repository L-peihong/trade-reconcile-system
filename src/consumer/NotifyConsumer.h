// 下游通知消费者（MQ 消费侧）
// 手动 ACK：处理成功才 basic_ack；失败 basic_nack(requeue=false) 转死信，不做无限重投。
// 幂等：uk_message_id 去重，重复投递直接 ACK 跳过（至少一次语义的必然产物）。
// 消费者内指数退避重试 2 次（1s/2s），仍失败转死信。
//
// 独立常驻线程跑阻塞式消费循环；DB 与 HTTP 通知均为同步等待，
// 不在事件循环线程上。V1 不做优雅退出，detach 后随进程终止。
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
