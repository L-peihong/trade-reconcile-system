// 幂等表 DAO。
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
    // 用 ON DUPLICATE KEY UPDATE request_id = request_id（无操作更新）区分插入
    // 与冲突：Drogon 的 MySQL 连接未设 CLIENT_FOUND_ROWS，无操作更新返回 0 行，
    // 新插入返回 1；非 1 一律走查询分支，保守。
    IdempotentOutcome tryAcquire(
        const std::shared_ptr<drogon::orm::Transaction>& tx,
        const models::Idempotent& record);

    // 业务成功后写回响应快照。带 status = 0 条件，只允许「处理中 → 成功」。
    void markSuccess(const std::shared_ptr<drogon::orm::Transaction>& tx,
                     const std::string& requestId,
                     const std::string& responseBody);
};

}  // namespace dao
