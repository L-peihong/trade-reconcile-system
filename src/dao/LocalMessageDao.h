// 本地消息表 DAO。与业务写操作同事务 INSERT，
// 投递由 MessageRelayTask 完成，这里只管落库。
#pragma once

#include <memory>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>

#include "models/LocalMessage.h"

namespace dao {

class LocalMessageDao {
public:
    // 插入待发送消息。next_retry_time 由 DB 侧 NOW() 生成（时区口径一致）。
    void insert(const std::shared_ptr<drogon::orm::Transaction>& tx,
                const models::LocalMessage& message);

    // ---- 以下为投递侧接口（MessageRelayTask 用），非事务单条操作 ----

    // 扫描待发送且到期的消息（status=0 AND next_retry_time <= NOW()），按 id 升序。
    std::vector<models::LocalMessage> scanPending(int limit);

    // 投递成功（publisher confirm）→ status=2。带 status=0 条件防重复处理。
    void markAcked(const std::string& messageId);

    // 投递失败 → retry_count 更新 + 指数退避的下次重试时间
    void markFailed(const std::string& messageId, int retryCount, int backoffSeconds);

    // 重试超限 → status=3（放弃），由监控告警发现
    void markGiveUp(const std::string& messageId);
};

}  // namespace dao
