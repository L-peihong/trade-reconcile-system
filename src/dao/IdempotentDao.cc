#include "dao/IdempotentDao.h"

#include <cstdint>

namespace dao {

IdempotentOutcome IdempotentDao::tryAcquire(
    const std::shared_ptr<drogon::orm::Transaction>& tx,
    const models::Idempotent& record) {

    // 注意：execSqlSync 返回 Result 值不是指针，用 `.` 访问。
    auto insertResult = tx->execSqlSync(
        "INSERT INTO t_idempotent"
        " (request_id, user_id, api_path, request_hash, status, expire_time)"
        " VALUES (?, ?, ?, ?, 0, ?)"
        " ON DUPLICATE KEY UPDATE request_id = request_id",
        record.requestId,
        static_cast<long long>(record.userId),
        record.apiPath,
        record.requestHash,
        record.expireTime);

    if (insertResult.affectedRows() == 1) {
        return {IdempotentOutcome::Kind::kAcquired, ""};
    }

    // 唯一键冲突：查状态定生死。
    auto query = tx->execSqlSync(
        "SELECT status, response_body FROM t_idempotent WHERE request_id = ?",
        record.requestId);
    if (query.empty()) {
        // 理论不可达（刚插入却查不到），保守按「处理中」返回让客户端重试。
        return {IdempotentOutcome::Kind::kProcessing, ""};
    }

    const auto& row = query[0];
    if (row["status"].as<int>() == 1) {
        return {IdempotentOutcome::Kind::kCachedSuccess,
                row["response_body"].as<std::string>()};
    }
    return {IdempotentOutcome::Kind::kProcessing, ""};
}

void IdempotentDao::markSuccess(
    const std::shared_ptr<drogon::orm::Transaction>& tx,
    const std::string& requestId,
    const std::string& responseBody) {

    tx->execSqlSync(
        "UPDATE t_idempotent"
        "   SET status = 1, response_body = ?, updated_at = NOW()"
        " WHERE request_id = ? AND status = 0",
        responseBody,
        requestId);
}

}  // namespace dao
