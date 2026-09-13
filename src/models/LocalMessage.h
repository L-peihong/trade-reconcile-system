// ============================================================================
// 本地消息表模型（CLAUDE.md 5.7）—— 可靠消息的地基
// 与业务写操作同事务 INSERT（CLAUDE.md 6.3），由 MessageRelayTask 投递（链路④）。
// ============================================================================
#pragma once

#include <string>

namespace models {

struct LocalMessage {
    enum class Status : int {
        kPending = 0,   // 待发送
        kSent    = 1,   // 已发送
        kAcked   = 2,   // 已确认（publisher confirm）
        kFailed  = 3,   // 发送失败（超限）
    };

    std::string messageId;        // 雪花，uk_message_id（消费端幂等键）
    std::string bizType;          // 如 ORDER_PAID
    std::string bizId;            // 业务主键，如 order_no
    std::string exchange;         // 投递交换机
    std::string routingKey;       // 投递路由键
    std::string payload;          // 消息体 JSON
    Status status = Status::kPending;
    int retryCount = 0;
    std::string nextRetryTime;    // DB 侧 NOW() 生成
};

}  // namespace models
