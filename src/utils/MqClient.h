// RabbitMQ 客户端封装（rabbitmq-c），可靠消息的投递端。
//   - 消息持久化（delivery_mode=2），broker 重启不丢；
//   - 每次 publish 后同步等 publisher confirm，确认送达才算成功；
//   - 内部互斥，rabbitmq-c 连接非线程安全，同一时刻只允许一个发布者；
//   - 失败重连一次，持续失败由调用方（MessageRelayTask）指数退避重试。
// 连接/等待都是阻塞 IO，只能在工作线程池调用，别上 Drogon 事件循环线程。
#pragma once

#include <mutex>
#include <string>

#include <amqp.h>

namespace utils {

struct MqConfig {
    std::string host = "127.0.0.1";
    int port = 5672;
    std::string user = "guest";
    std::string password = "guest";
    std::string vhost = "/";
};

class MqClient {
public:
    explicit MqClient(const MqConfig& config);
    ~MqClient();

    MqClient(const MqClient&) = delete;
    MqClient& operator=(const MqClient&) = delete;

    // 发布持久化消息并同步等 publisher confirm。
    // true = broker 已确认；false = 发送失败/确认超时（连接已关，下轮重连）。
    bool publish(const std::string& exchange,
                 const std::string& routingKey,
                 const std::string& payload);

private:
    bool ensureConnected();
    void close();
    bool declareExchange();
    bool waitConfirm(int timeoutSeconds);

    MqConfig config_;
    amqp_connection_state_t conn_ = nullptr;  // 仅在 publish 持有互斥时访问
    std::mutex mutex_;
};

}  // namespace utils
