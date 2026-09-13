#include "dao/NotifyDao.h"

#include <drogon/drogon.h>

#include "utils/IdGenerator.h"

namespace dao {

NotifyInsertResult NotifyDao::insertWithDedup(const std::string& messageId,
                                              const std::string& orderNo,
                                              const std::string& targetUrl) {
    const std::string notifyNo = utils::IdGenerator::nextId();

    auto result = drogon::app().getDbClient()->execSqlSync(
        "INSERT INTO t_notify_record"
        " (notify_no, order_no, target_url, status, retry_count, message_id)"
        " VALUES (?, ?, ?, 0, 0, ?)"
        " ON DUPLICATE KEY UPDATE notify_no = notify_no",
        notifyNo, orderNo, targetUrl, messageId);

    if (result.affectedRows() == 1) {
        return {NotifyInsertOutcome::kInserted, notifyNo};
    }
    return {NotifyInsertOutcome::kDuplicate, ""};
}

void NotifyDao::markSuccess(const std::string& notifyNo,
                            const std::string& responseBody) {
    drogon::app().getDbClient()->execSqlSync(
        "UPDATE t_notify_record"
        "   SET status = 1, response_body = ?, updated_at = NOW()"
        " WHERE notify_no = ?",
        responseBody, notifyNo);
}

void NotifyDao::markFailed(const std::string& notifyNo,
                           int retryCount,
                           const std::string& responseBody) {
    drogon::app().getDbClient()->execSqlSync(
        "UPDATE t_notify_record"
        "   SET status = 2, retry_count = ?, response_body = ?, updated_at = NOW()"
        " WHERE notify_no = ?",
        retryCount, responseBody, notifyNo);
}

}  // namespace dao
