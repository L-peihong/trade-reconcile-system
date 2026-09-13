// ============================================================================
// 对账批次/差异明细 DAO —— 写操作在 Service 事务内（CLAUDE.md 4.1），
// 查询与核销为非事务单条操作（线程池线程调用，CLAUDE.md 6.6）。
// ============================================================================
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>

#include "models/Reconcile.h"

namespace dao {

class ReconcileDao {
public:
    // ---- 事务内写操作（由 ReconcileService 传入事务）----
    void insertBatch(const std::shared_ptr<drogon::orm::Transaction>& tx,
                     const models::ReconcileBatch& batch);
    void insertDiff(const std::shared_ptr<drogon::orm::Transaction>& tx,
                    const models::ReconcileDiff& diff);

    // ---- 非事务操作 ----
    // 同日期同渠道是否已有批次（uk_date_channel 兜底 + 定时任务幂等检查）
    bool hasBatchForDate(const std::string& billDate, const std::string& channel);

    // 差异查询。handleStatus = -1 表示全部。
    std::vector<models::ReconcileDiff> listDiffs(const std::string& batchNo,
                                                 int handleStatus,
                                                 int limit);

    // 按 ID 查单条差异（核销前的存在性检查）
    std::optional<models::ReconcileDiff> getDiffById(std::int64_t id);

    // 核销/忽略：带 handle_status=0 条件，返回受影响行数（0 = 已处理过）
    std::size_t handleDiff(std::int64_t id, int handleStatus, const std::string& remark);
};

}  // namespace dao
