// ============================================================================
// 对账服务（链路⑤，亮点④）—— 事务边界所在层（CLAUDE.md 4.1）
// 线程模型：方法在工作线程池线程上被调用，内部同步事务。
// ============================================================================
#pragma once

#include <memory>
#include <string>
#include <vector>

#include <jsoncpp/json/json.h>

#include "models/Reconcile.h"
#include "models/Result.h"

namespace dao {
class ReconcileDao;
}

namespace services {

// 分类后的差异（纯数据，方便单测）
struct ClassifiedDiff {
    models::ReconcileDiffType type = models::ReconcileDiffType::kLocalOnly;
    std::string channelTradeNo;
    std::string orderNo;
    std::int64_t localFen = 0;
    std::int64_t channelFen = 0;
    std::int64_t diffFen = 0;  // 差额 = 本地 − 渠道
};

// 纯函数（可单测）：以 channelTradeNo 为键逐笔核对。
//   本地有渠道无 → 本地多单(1)；渠道有本地无 → 渠道多单(2)；
//   两侧都有但金额不等 → 金额不一致(3)。diffFen = 本地 − 渠道。
std::vector<ClassifiedDiff> classifyReconcileDiffs(
    const std::vector<models::ReconcileSideRow>& local,
    const std::vector<models::ReconcileSideRow>& channel);

class ReconcileService {
public:
    ReconcileService();
    ~ReconcileService();

    // 对指定账单日期跑批：读渠道账单 → 拉本地流水 → 逐笔核对 → 批次+差异落库。
    // 账单文件路径：scripts/bills/bill_<billDate>.csv（相对仓库根）。
    // 同日期同渠道已有批次时拒绝（uk_date_channel 双保险）。
    models::Result<Json::Value> run(const std::string& billDate);

    // 差异查询（handleStatus 缺省全部）
    models::Result<Json::Value> listDiffs(const std::string& batchNo, int limit);

    // 核销/忽略单条差异。action: "verify" | "ignore"
    models::Result<Json::Value> handleDiff(std::int64_t diffId,
                                           const std::string& action,
                                           const std::string& remark);

private:
    std::unique_ptr<dao::ReconcileDao> dao_;
};

}  // namespace services
