// ============================================================================
// 支付记录表 DAO —— 单表 CRUD，不开事务（CLAUDE.md 4.1）
// ============================================================================
#pragma once

#include <memory>
#include <optional>
#include <string>

#include <drogon/orm/DbClient.h>

#include "models/Payment.h"

namespace dao {

// 渠道幂等插入的结果
enum class PaymentInsertOutcome {
    kInserted,   // 新插入，本次回调获得处理权
    kDuplicate,  // 渠道交易号已存在（重复回调）
};

class PaymentDao {
public:
    // 渠道幂等落库（CLAUDE.md 5.5）：uk_channel_trade 唯一索引 + ODKU 无操作更新
    // 区分插入与冲突（同 IdempotentDao 的判定法，Drogon 未设 CLIENT_FOUND_ROWS）。
    PaymentInsertOutcome insertWithDedup(
        const std::shared_ptr<drogon::orm::Transaction>& tx,
        const models::Payment& payment);

    // 按（渠道, 渠道交易号）查询，用于重复回调时判断归属订单
    std::optional<models::Payment> getByChannelTradeNo(
        const std::shared_ptr<drogon::orm::Transaction>& tx,
        const std::string& channel,
        const std::string& channelTradeNo);
};

}  // namespace dao
