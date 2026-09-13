#include "tasks/MessageRelayTask.h"

#include <drogon/drogon.h>

#include <string>

#include "common/Logger.h"
#include "dao/LocalMessageDao.h"
#include "models/LocalMessage.h"
#include "utils/MqClient.h"
#include "utils/ThreadPool.h"

namespace tasks {
namespace {

constexpr double kScanIntervalSeconds = 5.0;
constexpr int kBatchLimit = 200;
constexpr int kMaxRetries = 8;          // 超过上限置 status=3 并告警
constexpr int kBaseBackoffSeconds = 5;  // 指数退避基数：5s × 2^(n-1)

utils::MqConfig loadMqConfig() {
    utils::MqConfig config;
    const auto& cfg = drogon::app().getCustomConfig();
    if (cfg.isMember("rabbitmq")) {
        const auto& r = cfg["rabbitmq"];
        config.host     = r.isMember("host") ? r["host"].asString() : config.host;
        config.port     = r.isMember("port") ? r["port"].asInt() : config.port;
        config.user     = r.isMember("user") ? r["user"].asString() : config.user;
        config.password = r.isMember("password") ? r["password"].asString() : config.password;
        config.vhost    = r.isMember("vhost") ? r["vhost"].asString() : config.vhost;
    }
    return config;
}

}  // namespace

MessageRelayTask& MessageRelayTask::instance() {
    static MessageRelayTask task;
    return task;
}

void MessageRelayTask::start() {
    // 回调在事件循环线程上，只做防重入 + 提交，不阻塞
    drogon::app().getLoop()->runEvery(kScanIntervalSeconds, []() {
        MessageRelayTask::instance().runOnce();
    });
}

void MessageRelayTask::runOnce() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;  // 上一批还没处理完，直接跳过
    }
    utils::globalThreadPool().submit([]() {
        MessageRelayTask::instance().relay();
        MessageRelayTask::instance().running_.store(false);
    });
}

void MessageRelayTask::relay() {
    auto log = common::Logger::get();

    // 懒加载：首次扫描时创建（此时 app 配置已加载完毕）
    if (!mqClient_) {
        mqClient_ = std::make_unique<utils::MqClient>(loadMqConfig());
    }

    dao::LocalMessageDao dao;
    const auto messages = dao.scanPending(kBatchLimit);
    if (messages.empty()) {
        return;
    }

    std::size_t sent = 0;
    std::size_t retried = 0;
    std::size_t givenUp = 0;

    for (const auto& msg : messages) {
        try {
            // 至少一次语义：confirm 成功但状态更新失败时下轮会重复投递，
            // 由消费端幂等兜底
            if (mqClient_->publish(msg.exchange, msg.routingKey, msg.payload)) {
                dao.markAcked(msg.messageId);
                ++sent;
            } else {
                const int next = msg.retryCount + 1;
                if (next >= kMaxRetries) {
                    dao.markGiveUp(msg.messageId);
                    ++givenUp;
                    log->error("message relay give up: id={} biz_type={}",
                               msg.messageId, msg.bizType);
                } else {
                    const int backoffSeconds =
                        kBaseBackoffSeconds * (1 << (next - 1));
                    dao.markFailed(msg.messageId, next, backoffSeconds);
                    ++retried;
                }
            }
        } catch (const std::exception& e) {
            // 逐条隔离，一条失败不中断整批
            log->error("message relay exception: id={} err={}", msg.messageId, e.what());
            ++retried;
        }
    }

    log->info("message relay round done: sent={} retry_scheduled={} giveup={}",
              sent, retried, givenUp);
}

}  // namespace tasks
