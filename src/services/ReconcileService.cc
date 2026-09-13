#include "services/ReconcileService.h"

#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <drogon/drogon.h>

#include "common/BizException.h"
#include "common/ErrorCode.h"
#include "common/Logger.h"
#include "dao/ReconcileDao.h"
#include "utils/DbUtil.h"
#include "utils/IdGenerator.h"
#include "utils/MoneyUtil.h"

namespace services {

std::vector<ClassifiedDiff> classifyReconcileDiffs(
    const std::vector<models::ReconcileSideRow>& local,
    const std::vector<models::ReconcileSideRow>& channel) {

    std::map<std::string, models::ReconcileSideRow> localByTradeNo;
    for (const auto& row : local) {
        localByTradeNo[row.channelTradeNo] = row;
    }
    std::map<std::string, models::ReconcileSideRow> channelByTradeNo;
    for (const auto& row : channel) {
        channelByTradeNo[row.channelTradeNo] = row;
    }

    std::vector<ClassifiedDiff> diffs;

    // 本地有、渠道无 → 本地多单
    for (const auto& [tradeNo, localRow] : localByTradeNo) {
        if (channelByTradeNo.find(tradeNo) == channelByTradeNo.end()) {
            ClassifiedDiff d;
            d.type = models::ReconcileDiffType::kLocalOnly;
            d.channelTradeNo = tradeNo;
            d.orderNo = localRow.orderNo;
            d.localFen = localRow.amountFen;
            d.diffFen = localRow.amountFen;  // 渠道侧视为 0
            diffs.push_back(std::move(d));
        }
    }

    // 渠道有、本地无 → 渠道多单
    for (const auto& [tradeNo, channelRow] : channelByTradeNo) {
        if (localByTradeNo.find(tradeNo) == localByTradeNo.end()) {
            ClassifiedDiff d;
            d.type = models::ReconcileDiffType::kChannelOnly;
            d.channelTradeNo = tradeNo;
            d.orderNo = channelRow.orderNo;
            d.channelFen = channelRow.amountFen;
            d.diffFen = -channelRow.amountFen;  // 本地侧视为 0
            diffs.push_back(std::move(d));
        }
    }

    // 两侧都有 → 金额核对
    for (const auto& [tradeNo, localRow] : localByTradeNo) {
        auto it = channelByTradeNo.find(tradeNo);
        if (it != channelByTradeNo.end() && it->second.amountFen != localRow.amountFen) {
            ClassifiedDiff d;
            d.type = models::ReconcileDiffType::kAmountMismatch;
            d.channelTradeNo = tradeNo;
            d.orderNo = localRow.orderNo;
            d.localFen = localRow.amountFen;
            d.channelFen = it->second.amountFen;
            d.diffFen = localRow.amountFen - it->second.amountFen;
            diffs.push_back(std::move(d));
        }
    }

    return diffs;
}

namespace {
constexpr const char* kChannel = "MOCK";
constexpr int kDiffQueryLimit = 200;

// 解析一行账单:channelTradeNo,orderNo,amount,callbackTime
bool parseBillLine(const std::string& line, models::ReconcileSideRow& row) {
    std::vector<std::string> fields;
    std::istringstream stream(line);
    std::string field;
    while (std::getline(stream, field, ',')) {
        fields.push_back(field);
    }
    if (fields.size() < 4) {
        return false;
    }
    row.channelTradeNo = fields[0];
    row.orderNo = fields[1];
    const auto fen = utils::yuanStringToFen(fields[2]);
    if (!fen.has_value()) {
        return false;
    }
    row.amountFen = *fen;
    // fields[3] 是回调时间，核对用不到，但保留解析（防账单格式悄悄变化）
    return true;
}
}  // namespace

ReconcileService::ReconcileService()
    : dao_(std::make_unique<dao::ReconcileDao>()) {}

ReconcileService::~ReconcileService() = default;

models::Result<Json::Value> ReconcileService::run(const std::string& billDate) {
    auto log = common::Logger::get();

    // ---- 1. 读渠道账单（事务外，文件 IO 不进事务）----
    const std::string billPath = "scripts/bills/bill_" + billDate + ".csv";
    std::ifstream billFile(billPath);
    if (!billFile.is_open()) {
        log->warn("reconcile: bill file not found: {}", billPath);
        return models::Result<Json::Value>::fail(
            common::ErrCode::kReconcileBillParseFailed,
            "账单文件不存在（先用 scripts/gen_bill.sh 生成）", billDate);
    }

    std::vector<models::ReconcileSideRow> channelRows;
    {
        std::string line;
        while (std::getline(billFile, line)) {
            if (line.empty() || line[0] == '#') {
                continue;
            }
            models::ReconcileSideRow row;
            if (!parseBillLine(line, row)) {
                log->error("reconcile: bad bill line: {}", line);
                return models::Result<Json::Value>::fail(
                    common::ErrCode::kReconcileBillParseFailed,
                    "账单行解析失败", billDate);
            }
            channelRows.push_back(std::move(row));
        }
    }

    try {
        utils::TransactionGuard txGuard(utils::beginTransaction());
        auto tx = txGuard.tx();

        // ---- 2. 同日期同渠道防重（uk_date_channel 兜底 + 明确报错）----
        if (dao_->hasBatchForDate(billDate, kChannel)) {
            throw common::BizException(common::ErrCode::kReconcileBatchStatusInvalid,
                                       "该日期已对账，勿重复跑批");
        }

        // ---- 3. 本地流水（渠道口径：按 callback_time 归集，CLAUDE.md 6.9）----
        auto result = tx->execSqlSync(
            "SELECT channel_trade_no, order_no, CAST(amount * 100 AS SIGNED) AS amount_fen"
            "  FROM t_payment"
            " WHERE channel = ? AND status = 1"
            "   AND callback_time >= ?"
            "   AND callback_time < DATE_ADD(?, INTERVAL 1 DAY)"
            " ORDER BY id ASC",
            kChannel, billDate + " 00:00:00", billDate + " 00:00:00");

        std::vector<models::ReconcileSideRow> localRows;
        localRows.reserve(static_cast<std::size_t>(result.size()));
        for (std::size_t i = 0; i < result.size(); ++i) {
            const auto& row = result[i];
            models::ReconcileSideRow r;
            r.channelTradeNo = row["channel_trade_no"].as<std::string>();
            r.orderNo        = row["order_no"].as<std::string>();
            r.amountFen      = row["amount_fen"].as<long long>();
            localRows.push_back(std::move(r));
        }

        // ---- 4. 逐笔核对（纯函数,已单测）----
        const auto classified = classifyReconcileDiffs(localRows, channelRows);

        // ---- 5. 批次汇总 + 差异落库（同一事务）----
        std::int64_t localTotalFen = 0;
        std::int64_t channelTotalFen = 0;
        for (const auto& row : localRows) localTotalFen += row.amountFen;
        for (const auto& row : channelRows) channelTotalFen += row.amountFen;

        models::ReconcileBatch batch;
        batch.batchNo          = utils::IdGenerator::nextId();
        batch.billDate         = billDate;
        batch.channel          = kChannel;
        batch.totalCount       = static_cast<std::int64_t>(localRows.size());
        batch.totalAmountFen   = localTotalFen;
        batch.channelCount     = static_cast<std::int64_t>(channelRows.size());
        batch.channelAmountFen = channelTotalFen;
        batch.diffCount        = static_cast<std::int64_t>(classified.size());
        batch.status = classified.empty() ? models::ReconcileBatch::Status::kBalanced
                                          : models::ReconcileBatch::Status::kHasDiff;
        dao_->insertBatch(tx, batch);

        for (const auto& d : classified) {
            models::ReconcileDiff diff;
            diff.batchNo         = batch.batchNo;
            diff.orderNo         = d.orderNo;
            diff.channelTradeNo  = d.channelTradeNo;
            diff.localAmountFen  = d.localFen;
            diff.channelAmountFen = d.channelFen;
            diff.diffType        = d.type;
            diff.diffAmountFen   = d.diffFen;
            dao_->insertDiff(tx, diff);
        }

        txGuard.commit();

        log->info("reconcile done: date={} batch={} local={} channel={} diffs={}",
                  billDate, batch.batchNo, localRows.size(), channelRows.size(),
                  classified.size());

        Json::Value data;
        data["batchNo"]   = batch.batchNo;
        data["diffCount"] = static_cast<Json::Int64>(batch.diffCount);
        data["status"]    = static_cast<int>(batch.status);
        return models::Result<Json::Value>::ok(std::move(data), billDate);

    } catch (const common::BizException& e) {
        return models::Result<Json::Value>::fail(e.code(), e.what(), billDate);
    } catch (const drogon::orm::DrogonDbException& e) {
        log->error("reconcile db error: {}", e.base().what());
        return models::Result<Json::Value>::fail(
            common::ErrCode::kDbError,
            common::toMessage(common::ErrCode::kDbError), billDate);
    } catch (const std::exception& e) {
        log->error("reconcile unexpected error: {}", e.what());
        return models::Result<Json::Value>::fail(
            common::ErrCode::kInternalError,
            common::toMessage(common::ErrCode::kInternalError), billDate);
    }
}

models::Result<Json::Value> ReconcileService::listDiffs(const std::string& batchNo,
                                                        int limit) {
    try {
        const auto diffs = dao_->listDiffs(batchNo, -1, limit);
        Json::Value data;
        Json::Value list(Json::arrayValue);
        for (const auto& d : diffs) {
            list.append(d.toJson());
        }
        data["diffs"] = list;
        data["count"] = static_cast<Json::Int64>(diffs.size());
        return models::Result<Json::Value>::ok(std::move(data), batchNo);
    } catch (const drogon::orm::DrogonDbException& e) {
        return models::Result<Json::Value>::fail(
            common::ErrCode::kDbError,
            common::toMessage(common::ErrCode::kDbError), batchNo);
    }
}

models::Result<Json::Value> ReconcileService::handleDiff(std::int64_t diffId,
                                                         const std::string& action,
                                                         const std::string& remark) {
    int handleStatus = -1;
    if (action == "verify") {
        handleStatus = static_cast<int>(models::ReconcileDiff::HandleStatus::kVerified);
    } else if (action == "ignore") {
        handleStatus = static_cast<int>(models::ReconcileDiff::HandleStatus::kIgnored);
    } else {
        return models::Result<Json::Value>::fail(
            common::ErrCode::kParamInvalid, "action 必须是 verify 或 ignore", "");
    }

    const auto existing = dao_->getDiffById(diffId);
    if (!existing.has_value()) {
        return models::Result<Json::Value>::fail(
            common::ErrCode::kReconcileBatchNotFound, "差异记录不存在", "");
    }

    // 带 handle_status=0 条件更新：并发/重复核销时靠受影响行数判定
    if (dao_->handleDiff(diffId, handleStatus, remark) == 0) {
        return models::Result<Json::Value>::fail(
            common::ErrCode::kReconcileDiffAlreadyHandled, "差异已处理过", "");
    }

    common::Logger::get()->info("reconcile diff handled: id={} action={}",
                                diffId, action);
    Json::Value data;
    data["diffId"]     = static_cast<Json::Int64>(diffId);
    data["handleStatus"] = handleStatus;
    return models::Result<Json::Value>::ok(std::move(data), "");
}

}  // namespace services
