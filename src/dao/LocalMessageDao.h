// ============================================================================
// 本地消息表 DAO —— 与业务写操作同事务 INSERT（CLAUDE.md 6.3）
// 投递由链路④的 MessageRelayTask 完成，本 DAO 只管落库。
// ============================================================================
#pragma once

#include <memory>

#include <drogon/orm/DbClient.h>

#include "models/LocalMessage.h"

namespace dao {

class LocalMessageDao {
public:
    // 插入待发送消息。next_retry_time 由 DB 侧 NOW() 生成（时区口径一致）。
    void insert(const std::shared_ptr<drogon::orm::Transaction>& tx,
                const models::LocalMessage& message);
};

}  // namespace dao
