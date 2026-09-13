#include "dao/AccountDao.h"

#include <cstdint>

#include "utils/MoneyUtil.h"

namespace dao {

std::optional<models::Account> AccountDao::getByUserId(
    const std::shared_ptr<drogon::orm::Transaction>& tx,
    std::uint64_t userId) {

    auto result = tx->execSqlSync(
        "SELECT account_no, user_id,"
        "       CAST(balance * 100 AS SIGNED)       AS balance_fen,"
        "       CAST(frozen_amount * 100 AS SIGNED) AS frozen_fen,"
        "       status"
        "  FROM t_account"
        " WHERE user_id = ?",
        static_cast<long long>(userId));

    if (result.empty()) {
        return std::nullopt;
    }

    const auto& row = result[0];
    models::Account a;
    a.accountNo       = row["account_no"].as<std::string>();
    a.userId          = static_cast<std::uint64_t>(row["user_id"].as<long long>());
    a.balanceFen      = row["balance_fen"].as<long long>();
    a.frozenAmountFen = row["frozen_fen"].as<long long>();
    a.status          = row["status"].as<int>();
    return a;
}

std::size_t AccountDao::deduct(
    const std::shared_ptr<drogon::orm::Transaction>& tx,
    const std::string& accountNo,
    std::int64_t amountFen) {

    // 条件扣减（CLAUDE.md 5.1 约定 + 6.1 思路推广）：
    //   WHERE balance >= ? —— 余额判定与扣减原子完成；
    //   AND status = 0    —— 冻结/注销账户不可动账。
    auto result = tx->execSqlSync(
        "UPDATE t_account"
        "   SET balance = balance - CAST(? AS DECIMAL(18,2)),"
        "       version = version + 1, updated_at = NOW()"
        " WHERE account_no = ?"
        "   AND status = 0"
        "   AND balance >= CAST(? AS DECIMAL(18,2))",
        utils::fenToYuanString(amountFen),
        accountNo,
        utils::fenToYuanString(amountFen));
    return result.affectedRows();
}

void AccountDao::insertFlow(
    const std::shared_ptr<drogon::orm::Transaction>& tx,
    const models::AccountFlow& flow) {

    // uk_request_biz(request_id, biz_type) 兜底：同一请求 ID 重复回调不会重复记账。
    tx->execSqlSync(
        "INSERT INTO t_account_flow"
        " (account_no, biz_type, direction, amount,"
        "  before_balance, after_balance, order_no, request_id)"
        " VALUES (?, ?, ?, CAST(? AS DECIMAL(18,2)),"
        "         CAST(? AS DECIMAL(18,2)), CAST(? AS DECIMAL(18,2)), ?, ?)",
        flow.accountNo,
        static_cast<long long>(flow.bizType),
        static_cast<long long>(flow.direction),
        utils::fenToYuanString(flow.amountFen),
        utils::fenToYuanString(flow.beforeFen),
        utils::fenToYuanString(flow.afterFen),
        flow.orderNo,
        flow.requestId);
}

}  // namespace dao
