// ============================================================================
// 对账模型（CLAUDE.md 5.9/5.10）—— 纯数据结构
// 金额一律「分」int64_t（CLAUDE.md 6.5）。
// ============================================================================
#pragma once

#include <cstdint>
#include <string>

#include <jsoncpp/json/json.h>

#include "utils/MoneyUtil.h"

namespace models {

// 差异类型（t_reconcile_diff.diff_type，CLAUDE.md 5.10）
enum class ReconcileDiffType : int {
    kLocalOnly     = 1,  // 本地多单
    kChannelOnly   = 2,  // 渠道多单
    kAmountMismatch = 3, // 金额不一致
};

// 参与对账核对的单侧行（本地流水 / 渠道账单统一用这个形状）
struct ReconcileSideRow {
    std::string channelTradeNo;  // 核对键
    std::string orderNo;
    std::int64_t amountFen = 0;
};

// 差异明细（t_reconcile_diff）
struct ReconcileDiff {
    enum class HandleStatus : int {
        kPending  = 0,  // 待处理
        kVerified = 1,  // 已核销
        kIgnored  = 2,  // 已忽略
    };

    std::string batchNo;
    std::string orderNo;
    std::string channelTradeNo;
    std::int64_t localAmountFen = 0;
    std::int64_t channelAmountFen = 0;
    ReconcileDiffType diffType = ReconcileDiffType::kLocalOnly;
    std::int64_t diffAmountFen = 0;  // 差额 = 本地 − 渠道
    HandleStatus handleStatus = HandleStatus::kPending;
    std::string handleRemark;
    std::string handledAt;

    Json::Value toJson() const {
        Json::Value v;
        v["batchNo"]         = batchNo;
        v["orderNo"]         = orderNo;
        v["channelTradeNo"]  = channelTradeNo;
        v["localAmount"]     = utils::fenToYuanString(localAmountFen);
        v["channelAmount"]   = utils::fenToYuanString(channelAmountFen);
        v["diffType"]        = static_cast<int>(diffType);
        v["diffAmount"]      = utils::fenToYuanString(diffAmountFen);
        v["handleStatus"]    = static_cast<int>(handleStatus);
        v["handleRemark"]    = handleRemark;
        v["handledAt"]       = handledAt;
        return v;
    }
};

// 对账批次（t_reconcile_batch）
struct ReconcileBatch {
    enum class Status : int {
        kProcessing = 0,  // 处理中
        kBalanced   = 1,  // 已平账
        kHasDiff    = 2,  // 有差异
        kFailed     = 3,  // 失败
    };

    std::string batchNo;
    std::string billDate;   // 账单日期（渠道口径，CLAUDE.md 6.9）
    std::string channel = "MOCK";
    std::int64_t totalCount = 0;         // 本地侧笔数
    std::int64_t totalAmountFen = 0;     // 本地侧总额
    std::int64_t channelCount = 0;       // 渠道侧笔数
    std::int64_t channelAmountFen = 0;   // 渠道侧总额
    std::int64_t diffCount = 0;          // 差异笔数
    Status status = Status::kProcessing;

    Json::Value toJson() const {
        Json::Value v;
        v["batchNo"]        = batchNo;
        v["billDate"]       = billDate;
        v["channel"]        = channel;
        v["totalCount"]     = static_cast<Json::Int64>(totalCount);
        v["totalAmount"]    = utils::fenToYuanString(totalAmountFen);
        v["channelCount"]   = static_cast<Json::Int64>(channelCount);
        v["channelAmount"]  = utils::fenToYuanString(channelAmountFen);
        v["diffCount"]      = static_cast<Json::Int64>(diffCount);
        v["status"]         = static_cast<int>(status);
        return v;
    }
};

}  // namespace models
