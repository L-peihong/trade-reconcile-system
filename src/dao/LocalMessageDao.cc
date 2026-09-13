#include "dao/LocalMessageDao.h"

#include <cstdint>

#include <drogon/drogon.h>

namespace dao {

void LocalMessageDao::insert(
    const std::shared_ptr<drogon::orm::Transaction>& tx,
    const models::LocalMessage& message) {

    tx->execSqlSync(
        "INSERT INTO t_local_message"
        " (message_id, biz_type, biz_id, exchange, routing_key,"
        "  payload, status, retry_count, next_retry_time)"
        " VALUES (?, ?, ?, ?, ?, ?, 0, 0, NOW())",
        message.messageId,
        message.bizType,
        message.bizId,
        message.exchange,
        message.routingKey,
        message.payload);
}

std::vector<models::LocalMessage> LocalMessageDao::scanPending(int limit) {
    auto result = drogon::app().getDbClient()->execSqlSync(
        "SELECT message_id, biz_type, biz_id, exchange, routing_key,"
        "       payload, retry_count"
        "  FROM t_local_message"
        " WHERE status = 0 AND next_retry_time <= NOW()"
        " ORDER BY id ASC"
        " LIMIT ?",
        limit);

    std::vector<models::LocalMessage> messages;
    messages.reserve(static_cast<std::size_t>(result.size()));
    for (std::size_t i = 0; i < result.size(); ++i) {
        const auto& row = result[i];
        models::LocalMessage m;
        m.messageId  = row["message_id"].as<std::string>();
        m.bizType    = row["biz_type"].as<std::string>();
        m.bizId      = row["biz_id"].as<std::string>();
        m.exchange   = row["exchange"].as<std::string>();
        m.routingKey = row["routing_key"].as<std::string>();
        m.payload    = row["payload"].as<std::string>();
        m.retryCount = row["retry_count"].as<int>();
        messages.push_back(std::move(m));
    }
    return messages;
}

void LocalMessageDao::markAcked(const std::string& messageId) {
    drogon::app().getDbClient()->execSqlSync(
        "UPDATE t_local_message"
        "   SET status = 2, updated_at = NOW()"
        " WHERE message_id = ? AND status = 0",
        messageId);
}

void LocalMessageDao::markFailed(const std::string& messageId,
                                 int retryCount,
                                 int backoffSeconds) {
    drogon::app().getDbClient()->execSqlSync(
        "UPDATE t_local_message"
        "   SET retry_count = ?,"
        "       next_retry_time = DATE_ADD(NOW(), INTERVAL ? SECOND),"
        "       updated_at = NOW()"
        " WHERE message_id = ?",
        retryCount,
        backoffSeconds,
        messageId);
}

void LocalMessageDao::markGiveUp(const std::string& messageId) {
    drogon::app().getDbClient()->execSqlSync(
        "UPDATE t_local_message"
        "   SET status = 3, updated_at = NOW()"
        " WHERE message_id = ?",
        messageId);
}

}  // namespace dao
