#include "dao/LocalMessageDao.h"

#include <cstdint>

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

}  // namespace dao
