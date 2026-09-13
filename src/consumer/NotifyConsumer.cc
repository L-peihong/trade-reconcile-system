#include "consumer/NotifyConsumer.h"

#include <amqp_tcp_socket.h>

#include <chrono>
#include <cstring>
#include <future>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#include <drogon/drogon.h>
#include <jsoncpp/json/json.h>

#include "common/Logger.h"
#include "dao/NotifyDao.h"
#include "utils/IdGenerator.h"

namespace consumer {
namespace {

constexpr amqp_channel_t kChannel = 1;
constexpr const char* kExchange      = "trade.exchange";
constexpr const char* kQueue         = "trade.notify.queue";
constexpr const char* kRoutingKey    = "order.paid";
constexpr const char* kDeadExchange  = "trade.dead.exchange";
constexpr const char* kDeadQueue     = "trade.dead.queue";
constexpr const char* kDeadRoutingKey = "order.dead";
constexpr const char* kNotifyPath    = "/api/v1/mock/notify";
constexpr int kMaxInProcessRetries   = 2;  // 消费者内重试(1s/2s),超限转死信
constexpr int kReconnectDelaySeconds = 3;

bool rpcOk(amqp_connection_state_t conn) {
    const amqp_rpc_reply_t reply = amqp_get_rpc_reply(conn);
    return reply.reply_type == AMQP_RESPONSE_NORMAL;
}

std::string readConfigString(const char* key, const std::string& fallback) {
    const auto& cfg = drogon::app().getCustomConfig();
    if (cfg.isMember("rabbitmq") && cfg["rabbitmq"].isMember(key)) {
        return cfg["rabbitmq"][key].asString();
    }
    return fallback;
}

}  // namespace

NotifyConsumer& NotifyConsumer::instance() {
    static NotifyConsumer consumer;
    return consumer;
}

void NotifyConsumer::start() {
    notifyBaseUrl_ = readConfigString("notify_base_url", notifyBaseUrl_);
    // 常驻线程 detach（V1 不做优雅退出，随进程终止 —— 见头文件说明）
    std::thread([this]() { runLoop(); }).detach();
    common::Logger::get()->info("notify consumer thread started");
}

void NotifyConsumer::runLoop() {
    while (true) {
        if (connectAndSubscribe()) {
            consumeLoop();  // 阻塞消费,连接断返回
        }
        std::this_thread::sleep_for(std::chrono::seconds(kReconnectDelaySeconds));
    }
}

bool NotifyConsumer::connectAndSubscribe() {
    auto log = common::Logger::get();

    conn_ = amqp_new_connection();
    amqp_socket_t* socket = amqp_tcp_socket_new(conn_);
    if (!socket) {
        amqp_destroy_connection(conn_);
        conn_ = nullptr;
        return false;
    }

    const auto& cfg = drogon::app().getCustomConfig()["rabbitmq"];
    const std::string host = cfg.isMember("host") ? cfg["host"].asString() : "127.0.0.1";
    const int port = cfg.isMember("port") ? cfg["port"].asInt() : 5673;
    const std::string user = cfg.isMember("user") ? cfg["user"].asString() : "guest";
    const std::string password = cfg.isMember("password") ? cfg["password"].asString() : "guest";
    const std::string vhost = cfg.isMember("vhost") ? cfg["vhost"].asString() : "/";

    if (amqp_socket_open(socket, host.c_str(), port) != AMQP_STATUS_OK) {
        log->warn("notify consumer connect failed: {}:{}", host, port);
        amqp_destroy_connection(conn_);
        conn_ = nullptr;
        return false;
    }
    const amqp_rpc_reply_t loginReply =
        amqp_login(conn_, vhost.c_str(), 0, 131072, 0, AMQP_SASL_METHOD_PLAIN,
                   user.c_str(), password.c_str());
    if (loginReply.reply_type != AMQP_RESPONSE_NORMAL) {
        log->warn("notify consumer login failed");
        amqp_connection_close(conn_, AMQP_REPLY_SUCCESS);
        amqp_destroy_connection(conn_);
        conn_ = nullptr;
        return false;
    }
    amqp_channel_open(conn_, kChannel);
    if (!rpcOk(conn_)) {
        amqp_connection_close(conn_, AMQP_REPLY_SUCCESS);
        amqp_destroy_connection(conn_);
        conn_ = nullptr;
        return false;
    }

    // ---- 拓扑声明（幂等）----
    // 主交换机
    amqp_exchange_declare(conn_, kChannel, amqp_cstring_bytes(kExchange),
                          amqp_cstring_bytes("topic"), 0, 1, 0, 0, amqp_empty_table);
    if (!rpcOk(conn_)) { return false; }
    // 死信交换机 + 死信队列 + 绑定
    amqp_exchange_declare(conn_, kChannel, amqp_cstring_bytes(kDeadExchange),
                          amqp_cstring_bytes("topic"), 0, 1, 0, 0, amqp_empty_table);
    if (!rpcOk(conn_)) { return false; }
    amqp_queue_declare(conn_, kChannel, amqp_cstring_bytes(kDeadQueue),
                       0, 1 /*durable*/, 0, 0, amqp_empty_table);
    if (!rpcOk(conn_)) { return false; }
    amqp_queue_bind(conn_, kChannel, amqp_cstring_bytes(kDeadQueue),
                    amqp_cstring_bytes(kDeadExchange),
                    amqp_cstring_bytes(kDeadRoutingKey), amqp_empty_table);
    if (!rpcOk(conn_)) { return false; }

    // 主队列：带死信参数（nack 且 requeue=false 的消息会路由到死信队列）
    amqp_table_entry_t deadLetterEntries[2];
    deadLetterEntries[0].key = amqp_cstring_bytes("x-dead-letter-exchange");
    deadLetterEntries[0].value.kind = AMQP_FIELD_KIND_UTF8;
    deadLetterEntries[0].value.value.bytes = amqp_cstring_bytes(kDeadExchange);
    deadLetterEntries[1].key = amqp_cstring_bytes("x-dead-letter-routing-key");
    deadLetterEntries[1].value.kind = AMQP_FIELD_KIND_UTF8;
    deadLetterEntries[1].value.value.bytes = amqp_cstring_bytes(kDeadRoutingKey);
    amqp_table_t queueArgs;
    queueArgs.num_entries = 2;
    queueArgs.entries = deadLetterEntries;
    amqp_queue_declare(conn_, kChannel, amqp_cstring_bytes(kQueue),
                       0, 1 /*durable*/, 0, 0, queueArgs);
    if (!rpcOk(conn_)) { return false; }

    amqp_queue_bind(conn_, kChannel, amqp_cstring_bytes(kQueue),
                    amqp_cstring_bytes(kExchange),
                    amqp_cstring_bytes(kRoutingKey), amqp_empty_table);
    if (!rpcOk(conn_)) { return false; }

    // 手动 ACK：no_ack = 0（CLAUDE.md 6.2）
    amqp_basic_consume(conn_, kChannel, amqp_cstring_bytes(kQueue),
                       amqp_empty_bytes /*consumer_tag*/, 0 /*no_local*/,
                       0 /*no_ack=0 → manual ack*/, 0 /*exclusive*/,
                       amqp_empty_table);
    if (!rpcOk(conn_)) { return false; }

    log->info("notify consumer subscribed: queue={}", kQueue);
    return true;
}

void NotifyConsumer::consumeLoop() {
    while (true) {
        amqp_maybe_release_buffers(conn_);
        amqp_envelope_t envelope;
        const amqp_rpc_reply_t reply =
            amqp_consume_message(conn_, &envelope, nullptr /*timeout*/, 0);
        if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
            common::Logger::get()->warn("notify consumer connection lost");
            amqp_connection_close(conn_, AMQP_REPLY_SUCCESS);
            amqp_destroy_connection(conn_);
            conn_ = nullptr;
            return;  // 交给 runLoop 重连
        }
        handleMessage(envelope);
        amqp_destroy_envelope(&envelope);
    }
}

void NotifyConsumer::handleMessage(const amqp_envelope_t& envelope) {
    const std::string body(
        reinterpret_cast<const char*>(envelope.message.body.bytes),
        envelope.message.body.len);
    const std::uint64_t deliveryTag = envelope.delivery_tag;

    // 处理成功才 ACK；失败 nack(requeue=false) 转死信（CLAUDE.md 6.2）。
    // 连接在 ACK 前断开 → broker 自动重投 → 消费幂等兜底（至少一次语义）。
    if (process(body)) {
        amqp_basic_ack(conn_, kChannel, deliveryTag, 0);
    } else {
        amqp_basic_nack(conn_, kChannel, deliveryTag, 0, 0 /*requeue=false*/);
    }
}

bool NotifyConsumer::process(const std::string& body) {
    auto log = common::Logger::get();

    Json::Value json;
    Json::CharReaderBuilder readerBuilder;
    std::string errors;
    std::istringstream stream(body);
    if (!Json::parseFromStream(readerBuilder, stream, &json, &errors)) {
        // 毒消息：无法解析 → 死信,人工介入(重试无意义)
        log->error("notify consumer: malformed message body: {}", body);
        return false;
    }
    if (!json.isMember("messageId") || !json.isMember("orderNo")) {
        log->error("notify consumer: missing required fields in message");
        return false;
    }

    const std::string messageId = json["messageId"].asString();
    const std::string orderNo   = json["orderNo"].asString();
    const std::string amount    = json.isMember("amount") ? json["amount"].asString() : "";

    dao::NotifyDao dao;

    // 幂等落库（uk_message_id）：重复投递直接 ACK 跳过
    const auto outcome = dao.insertWithDedup(messageId, orderNo, notifyBaseUrl_ + kNotifyPath);
    if (outcome.outcome == dao::NotifyInsertOutcome::kDuplicate) {
        log->info("notify consumer: duplicate message, skip: msg_id={}", messageId);
        return true;
    }

    // 商户通知：消费者内指数退避重试(1s/2s),超限 nack 转死信
    const std::string notifyNo = outcome.notifyNo;
    int attempt = 0;
    for (;;) {
        auto client = drogon::HttpClient::newHttpClient(notifyBaseUrl_);
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setMethod(drogon::Post);
        req->setPath(kNotifyPath);
        req->setContentTypeCode(drogon::CT_APPLICATION_JSON);
        req->addHeader("X-Request-Id", notifyNo);  // 雪花 ID 是十六进制字符集,可过过滤器

        Json::Value notifyBody;
        notifyBody["orderNo"] = orderNo;
        notifyBody["amount"]  = amount;
        req->setBody(notifyBody.toStyledString());

        // 同步等待:回调在事件循环完成,消费者线程阻塞在 future 上 —— 不死锁
        std::promise<std::pair<drogon::ReqResult, drogon::HttpResponsePtr>> promise;
        auto future = promise.get_future();
        client->sendRequest(
            req, [&promise](drogon::ReqResult result, const drogon::HttpResponsePtr& resp) {
                promise.set_value({result, resp});
            });
        const auto [result, resp] = future.get();

        if (result == drogon::ReqResult::Ok && resp &&
            resp->getStatusCode() == drogon::k200OK) {
            dao.markSuccess(notifyNo, std::string(resp->body()));
            log->info("notify consumer: merchant notified: order={} msg_id={}",
                      orderNo, messageId);
            return true;
        }

        ++attempt;
        if (attempt > kMaxInProcessRetries) {
            const std::string err =
                resp ? std::string(resp->body()) : "request failed";
            dao.markFailed(notifyNo, attempt, err);
            log->error("notify consumer: merchant unreachable, dead-letter: order={} attempts={}",
                       orderNo, attempt);
            return false;  // → nack(requeue=false) → 死信队列
        }
        std::this_thread::sleep_for(std::chrono::seconds(attempt));
    }
}

}  // namespace consumer
