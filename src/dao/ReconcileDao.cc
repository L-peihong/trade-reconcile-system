#include "dao/ReconcileDao.h"

#include <cstdint>

#include <drogon/drogon.h>

#include "utils/MoneyUtil.h"

namespace dao {

void ReconcileDao::insertBatch(
    const std::shared_ptr<drogon::orm::Transaction>& tx,
    const models::ReconcileBatch& batch) {
    tx->execSqlSync(
        "INSERT INTO t_reconcile_batch"
        " (batch_no, bill_date, channel, total_count, total_amount,"
        "  channel_count, channel_amount, diff_count, status)"
        " VALUES (?, ?, ?, ?, CAST(? AS DECIMAL(18,2)),"
        "         ?, CAST(? AS DECIMAL(18,2)), ?, ?)",
        batch.batchNo,
        batch.billDate,
        batch.channel,
        static_cast<long long>(batch.totalCount),
        utils::fenToYuanString(batch.totalAmountFen),
        static_cast<long long>(batch.channelCount),
        utils::fenToYuanString(batch.channelAmountFen),
        static_cast<long long>(batch.diffCount),
        static_cast<long long>(batch.status));
}

void ReconcileDao::insertDiff(
    const std::shared_ptr<drogon::orm::Transaction>& tx,
    const models::ReconcileDiff& diff) {
    tx->execSqlSync(
        "INSERT INTO t_reconcile_diff"
        " (batch_no, order_no, channel_trade_no, local_amount, channel_amount,"
        "  diff_type, diff_amount, handle_status)"
        " VALUES (?, ?, ?, CAST(? AS DECIMAL(18,2)), CAST(? AS DECIMAL(18,2)),"
        "         ?, CAST(? AS DECIMAL(18,2)), 0)",
        diff.batchNo,
        diff.orderNo,
        diff.channelTradeNo,
        utils::fenToYuanString(diff.localAmountFen),
        utils::fenToYuanString(diff.channelAmountFen),
        static_cast<long long>(diff.diffType),
        utils::fenToYuanString(diff.diffAmountFen));
}

bool ReconcileDao::hasBatchForDate(const std::string& billDate,
                                   const std::string& channel) {
    auto result = drogon::app().getDbClient()->execSqlSync(
        "SELECT COUNT(*) AS cnt FROM t_reconcile_batch"
        " WHERE bill_date = ? AND channel = ?",
        billDate, channel);
    return result[0]["cnt"].as<int>() > 0;
}

std::vector<models::ReconcileDiff> ReconcileDao::listDiffs(
    const std::string& batchNo, int handleStatus, int limit) {
    // 条件由参数拼装（参数化绑定，不拼字符串值）
    std::string sql =
        "SELECT id, batch_no, order_no, channel_trade_no,"
        "       CAST(local_amount * 100 AS SIGNED)   AS local_fen,"
        "       CAST(channel_amount * 100 AS SIGNED) AS channel_fen,"
        "       diff_type,"
        "       CAST(diff_amount * 100 AS SIGNED)    AS diff_fen,"
        "       handle_status,"
        "       IFNULL(handle_remark, '') AS handle_remark,"
        "       IFNULL(DATE_FORMAT(handled_at, '%Y-%m-%d %H:%i:%s'), '') AS handled_at"
        "  FROM t_reconcile_diff"
        " WHERE batch_no = ?";
    std::vector<models::ReconcileDiff> diffs;

    auto client = drogon::app().getDbClient();
    drogon::orm::Result result(nullptr);
    if (handleStatus >= 0) {
        sql += " AND handle_status = ? ORDER BY id ASC LIMIT ?";
        result = client->execSqlSync(sql, batchNo, handleStatus, limit);
    } else {
        sql += " ORDER BY id ASC LIMIT ?";
        result = client->execSqlSync(sql, batchNo, limit);
    }

    diffs.reserve(static_cast<std::size_t>(result.size()));
    for (std::size_t i = 0; i < result.size(); ++i) {
        const auto& row = result[i];
        models::ReconcileDiff d;
        d.batchNo         = row["batch_no"].as<std::string>();
        d.orderNo         = row["order_no"].as<std::string>();
        d.channelTradeNo  = row["channel_trade_no"].as<std::string>();
        d.localAmountFen  = row["local_fen"].as<long long>();
        d.channelAmountFen = row["channel_fen"].as<long long>();
        d.diffType = static_cast<models::ReconcileDiffType>(row["diff_type"].as<int>());
        d.diffAmountFen   = row["diff_fen"].as<long long>();
        d.handleStatus =
            static_cast<models::ReconcileDiff::HandleStatus>(row["handle_status"].as<int>());
        d.handleRemark    = row["handle_remark"].as<std::string>();
        d.handledAt       = row["handled_at"].as<std::string>();
        diffs.push_back(std::move(d));
    }
    return diffs;
}

std::optional<models::ReconcileDiff> ReconcileDao::getDiffById(std::int64_t id) {
    auto result = drogon::app().getDbClient()->execSqlSync(
        "SELECT id, batch_no, order_no, channel_trade_no,"
        "       CAST(local_amount * 100 AS SIGNED)   AS local_fen,"
        "       CAST(channel_amount * 100 AS SIGNED) AS channel_fen,"
        "       diff_type,"
        "       CAST(diff_amount * 100 AS SIGNED)    AS diff_fen,"
        "       handle_status,"
        "       IFNULL(handle_remark, '') AS handle_remark"
        "  FROM t_reconcile_diff WHERE id = ?",
        static_cast<long long>(id));

    if (result.empty()) {
        return std::nullopt;
    }
    const auto& row = result[0];
    models::ReconcileDiff d;
    d.batchNo         = row["batch_no"].as<std::string>();
    d.orderNo         = row["order_no"].as<std::string>();
    d.channelTradeNo  = row["channel_trade_no"].as<std::string>();
    d.localAmountFen  = row["local_fen"].as<long long>();
    d.channelAmountFen = row["channel_fen"].as<long long>();
    d.diffType = static_cast<models::ReconcileDiffType>(row["diff_type"].as<int>());
    d.diffAmountFen   = row["diff_fen"].as<long long>();
    d.handleStatus =
        static_cast<models::ReconcileDiff::HandleStatus>(row["handle_status"].as<int>());
    d.handleRemark    = row["handle_remark"].as<std::string>();
    return d;
}

std::size_t ReconcileDao::handleDiff(std::int64_t id, int handleStatus,
                                     const std::string& remark) {
    auto result = drogon::app().getDbClient()->execSqlSync(
        "UPDATE t_reconcile_diff"
        "   SET handle_status = ?, handle_remark = ?,"
        "       handled_at = NOW(), updated_at = NOW()"
        " WHERE id = ? AND handle_status = 0",
        handleStatus, remark, static_cast<long long>(id));
    return result.affectedRows();
}

}  // namespace dao
