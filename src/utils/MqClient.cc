#include "utils/MqClient.h"

#include <amqp_tcp_socket.h>

#include <cstring>
#include <sys/time.h>

#include "common/Logger.h"

namespace utils {
namespace {

constexpr amqp_channel_t kChannel = 1;
constexpr int kConfirmTimeoutSeconds = 3;
// 与 config.json custom_config.rabbitmq.exchange 保持一致（消息行里的
// exchange 字段即源于该配置，declare 与之一致才能投递）。
constexpr const char* kExchangeName = "trade.exchange";
constexpr const char* kExchangeType = "topic";

bool rpcOk(amqp_connection_state_t conn) {
    const amqp_rpc_reply_t reply = amqp_get_rpc_reply(conn);
    return reply.reply_type == AMQP_RESPONSE_NORMAL;
}

}  // namespace

MqClient::MqClient(const MqConfig& config) : config_(config) {}

MqClient::~MqClient() {
    close();
}

void MqClient::close() {
    if (conn_) {
        amqp_connection_close(conn_, AMQP_REPLY_SUCCESS);
        amqp_destroy_connection(conn_);
        conn_ = nullptr;
    }
}

bool MqClient::ensureConnected() {
    if (conn_) {
        return true;
    }

    auto log = common::Logger::get();

    conn_ = amqp_new_connection();
    amqp_socket_t* socket = amqp_tcp_socket_new(conn_);
    if (!socket) {
        close();
        return false;
    }
    if (amqp_socket_open(socket, config_.host.c_str(), config_.port) !=
        AMQP_STATUS_OK) {
        log->warn("mq connect failed: {}:{}", config_.host, config_.port);
        close();
        return false;
    }

    const amqp_rpc_reply_t loginReply =
        amqp_login(conn_, config_.vhost.c_str(), 0, 131072, 0,
                   AMQP_SASL_METHOD_PLAIN, config_.user.c_str(),
                   config_.password.c_str());
    if (loginReply.reply_type != AMQP_RESPONSE_NORMAL) {
        log->warn("mq login failed");
        close();
        return false;
    }

    amqp_channel_open(conn_, kChannel);
    if (!rpcOk(conn_)) {
        close();
        return false;
    }

    // ⚠️ 顺序不能换：exchange 声明必须在 confirm_select **之前** ——
    // confirm 模式下每条命令都有 ack 帧，声明在后会把它的 ack 混进
    // 第一条 publish 的 confirm 等待里，导致 ack 对应关系整体错位。
    if (!declareExchange()) {
        close();
        return false;
    }

    amqp_confirm_select(conn_, kChannel);
    if (!rpcOk(conn_)) {
        close();
        return false;
    }

    log->info("mq connected: {}:{}", config_.host, config_.port);
    return true;
}

bool MqClient::declareExchange() {
    amqp_exchange_declare(conn_, kChannel,
                          amqp_cstring_bytes(kExchangeName),
                          amqp_cstring_bytes(kExchangeType),
                          0 /*passive*/, 1 /*durable*/, 0 /*auto_delete*/,
                          0 /*internal*/, amqp_empty_table);
    return rpcOk(conn_);
}

bool MqClient::waitConfirm(int timeoutSeconds) {
    struct timeval tv;
    tv.tv_sec = timeoutSeconds;
    tv.tv_usec = 0;

    amqp_frame_t frame;
    const int status = amqp_simple_wait_frame_noblock(conn_, &frame, &tv);
    if (status != AMQP_STATUS_OK) {
        return false;  // 超时/IO 失败
    }
    if (frame.frame_type != AMQP_FRAME_METHOD) {
        return false;
    }
    return frame.payload.method.id == AMQP_BASIC_ACK_METHOD;
}

bool MqClient::publish(const std::string& exchange,
                       const std::string& routingKey,
                       const std::string& payload) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!ensureConnected()) {
        // 失败后再试一次重连（上次连接可能已半死）
        close();
        if (!ensureConnected()) {
            return false;
        }
    }

    amqp_basic_properties_t props;
    std::memset(&props, 0, sizeof(props));
    props._flags = AMQP_BASIC_DELIVERY_MODE_FLAG | AMQP_BASIC_CONTENT_TYPE_FLAG;
    props.delivery_mode = 2;  // 持久化：broker 重启消息不丢
    props.content_type = amqp_cstring_bytes("application/json");

    const int ret = amqp_basic_publish(conn_, kChannel,
                                       amqp_cstring_bytes(exchange.c_str()),
                                       amqp_cstring_bytes(routingKey.c_str()),
                                       0 /*mandatory*/, 0 /*immediate*/,
                                       &props,
                                       amqp_cstring_bytes(payload.c_str()));
    if (ret != AMQP_STATUS_OK) {
        common::Logger::get()->warn("mq publish send failed, status={}", ret);
        close();
        return false;
    }

    // 以 broker 的 confirm 为准（CLAUDE.md 6.3）：publish 不抛异常 ≠ 送达
    if (!waitConfirm(kConfirmTimeoutSeconds)) {
        common::Logger::get()->warn("mq publish confirm timeout/error");
        close();
        return false;
    }
    return true;
}

}  // namespace utils
