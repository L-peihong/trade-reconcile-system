// ============================================================================
// 幂等表 DAO（CLAUDE.md 5.6 语义边界）
// ============================================================================
#pragma once

#include <memory>
#include <string>

#include <drogon/orm/DbClient.h>

#include "models/Idempotent.h"

namespace dao {

// 抢占结果
struct IdempotentOutcome {
    enum class Kind {
        kAcquired,       // 抢占成功，获得执行权
        kProcessing,     // 唯一键冲突且 status=0：他人处理中 → 10003 稍后重试
        kCachedSuccess,  // 唯一键冲突且 status=1：返回首次成功快照
    };
    Kind kind = Kind::kAcquired;
    std::string cachedResponseBody;  // kCachedSuccess 时有效
};

class IdempotentDao {
public:
    // 抢占幂等键：INSERT 成功 = 获得执行权。
    // 实现细节（已核对 Drogon 源码）：用 INSERT ... ON DUPLICATE KEY UPDATE
    // request_id = request_id（无操作更新）区分插入与冲突 —— Drogon 的 MySQL
    // 连接 client_flag = 0（未设 CLIENT_FOUND_ROWS），无操作更新返回
    // affectedRows = 0，新插入返回 1；任何非 1 的值一律走查询分支，保守。
    IdempotentOutcome tryAcquire(
        const std::shared_ptr<drogon::orm::Transaction>& tx,
        const models::Idempotent& record);

    // 业务成功后写回响应快照。带 status = 0 条件，只允许「处理中 → 成功」。
    void markSuccess(const std::shared_ptr<drogon::orm::Transaction>& tx,
                     const std::string& requestId,
                     const std::string& responseBody);
};

}  // namespace dao
