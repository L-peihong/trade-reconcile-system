#include "dao/OrderDao.h"

#include <cstdint>

#include "utils/MoneyUtil.h"

namespace dao {

void OrderDao::insert(const std::shared_ptr<drogon::orm::Transaction>& tx,
                      const models::Order& order) {
    // 金额以「分」入参，经字符串 → DECIMAL 精确转换（CLAUDE.md 6.5，不经 double）。
    // expire_time 为应用侧计算值（修正 3）：时区对齐的前提是
    // 进程 TZ=Asia/Shanghai 且 MySQL --default-time-zone=+8:00（CLAUDE.md 6.7）。
    tx->execSqlSync(
        "INSERT INTO t_order"
        " (order_no, request_id, user_id, product_id, quantity,"
        "  unit_price, total_amount, status, expire_time)"
        " VALUES"
        " (?, ?, ?, ?, ?,"
        "  CAST(? AS DECIMAL(18,2)), CAST(? AS DECIMAL(18,2)), ?, ?)",
        order.orderNo,
        order.requestId,
        static_cast<long long>(order.userId),
        static_cast<long long>(order.productId),
        static_cast<long long>(order.quantity),
        utils::fenToYuanString(order.unitPriceFen),
        utils::fenToYuanString(order.totalAmountFen),
        static_cast<long long>(order.status == models::OrderStatus::kPendingPayment
                                   ? 0
                                   : static_cast<int>(order.status)),
        order.expireTime);
}

std::optional<models::Order> OrderDao::getByOrderNo(
    const std::shared_ptr<drogon::orm::Transaction>& tx,
    const std::string& orderNo) {

    // DECIMAL 在 SQL 内 CAST 成「分」取回（CLAUDE.md 6.5）；
    // 时间列统一 DATE_FORMAT 成字符串，不经驱动的时间类型中转。
    // execSqlSync 返回 Result **值**（非指针），用 `.` 访问
    auto result = tx->execSqlSync(
        "SELECT order_no, request_id, user_id, product_id, quantity,"
        "       CAST(unit_price * 100 AS SIGNED)   AS unit_price_fen,"
        "       CAST(total_amount * 100 AS SIGNED) AS total_amount_fen,"
        "       status,"
        "       DATE_FORMAT(expire_time, '%Y-%m-%d %H:%i:%s') AS expire_time,"
        "       IFNULL(DATE_FORMAT(paid_at, '%Y-%m-%d %H:%i:%s'), '') AS paid_at"
        "  FROM t_order"
        " WHERE order_no = ?",
        orderNo);

    if (result.empty()) {
        return std::nullopt;
    }

    const auto& row = result[0];
    models::Order o;
    o.orderNo        = row["order_no"].as<std::string>();
    o.requestId      = row["request_id"].as<std::string>();
    o.userId         = static_cast<std::uint64_t>(row["user_id"].as<long long>());
    o.productId      = static_cast<std::uint64_t>(row["product_id"].as<long long>());
    o.quantity       = row["quantity"].as<long long>();
    o.unitPriceFen   = row["unit_price_fen"].as<long long>();
    o.totalAmountFen = row["total_amount_fen"].as<long long>();
    o.status         = static_cast<models::OrderStatus>(row["status"].as<int>());
    o.expireTime     = row["expire_time"].as<std::string>();
    o.paidAt         = row["paid_at"].as<std::string>();
    return o;
}

}  // namespace dao
