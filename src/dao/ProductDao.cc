#include "dao/ProductDao.h"

#include <cstdint>

namespace dao {

std::size_t ProductDao::deductStock(
    const std::shared_ptr<drogon::orm::Transaction>& tx,
    std::uint64_t productId,
    std::int64_t quantity) {

    // 防超卖 SQL（CLAUDE.md 6.1）：
    //   WHERE stock >= ?  —— 判定与扣减在同一条语句内原子完成；
    //   AND status = 1    —— 下架商品不可售。
    // 禁止先 SELECT 再 UPDATE（TOCTOU）；也禁止 SELECT ... FOR UPDATE
    // （间隙锁放大锁冲突）。version 自增只是辅助审计，不参与判定。
    const auto result = tx->execSqlSync(
        "UPDATE t_product"
        "   SET stock = stock - ?, version = version + 1, updated_at = NOW()"
        " WHERE id = ? AND stock >= ? AND status = 1",
        static_cast<long long>(quantity),
        static_cast<long long>(productId),
        static_cast<long long>(quantity));
    return result->affectedRows();
}

std::optional<models::Product> ProductDao::getById(
    const std::shared_ptr<drogon::orm::Transaction>& tx,
    std::uint64_t productId) {

    // DECIMAL 不经 double 中转（CLAUDE.md 6.5）：SQL 内 CAST 成「分」取回 int64
    const auto result = tx->execSqlSync(
        "SELECT id, product_no, name,"
        "       CAST(price * 100 AS SIGNED) AS price_fen, stock, status"
        "  FROM t_product"
        " WHERE id = ?",
        static_cast<long long>(productId));

    if (result->empty()) {
        return std::nullopt;
    }

    const auto& row = (*result)[0];
    models::Product p;
    p.id        = static_cast<std::uint64_t>(row["id"].asInt64());
    p.productNo = row["product_no"].asString();
    p.name      = row["name"].asString();
    p.priceFen  = row["price_fen"].asInt64();
    p.stock     = row["stock"].asInt64();
    p.status    = row["status"].asInt();
    return p;
}

}  // namespace dao
