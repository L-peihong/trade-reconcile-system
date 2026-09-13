// 下游通知记录表 DAO，消费端幂等的地基。
// uk_message_id 去重，uk_notify_no 保证通知单唯一。
// 全为非事务单条操作，在消费者线程上调用。
#pragma once

#include <string>

namespace dao {

enum class NotifyInsertOutcome {
    kInserted,
    kDuplicate,
};

// 插入结果携带通知单号（kInserted 时有效，用于后续状态更新）
struct NotifyInsertResult {
    NotifyInsertOutcome outcome = NotifyInsertOutcome::kInserted;
    std::string notifyNo;
};

class NotifyDao {
public:
    // 幂等落库：ODKU 无操作更新判定插入/冲突（同 IdempotentDao 的判定法），初始 status=0。
    NotifyInsertResult insertWithDedup(const std::string& messageId,
                                       const std::string& orderNo,
                                       const std::string& targetUrl);

    // 通知成功 → status=1 + 商户响应原文
    void markSuccess(const std::string& notifyNo, const std::string& responseBody);

    // 消费者内重试耗尽 → status=2（消息将转死信，人工介入）
    void markFailed(const std::string& notifyNo, int retryCount,
                    const std::string& responseBody);
};

}  // namespace dao
