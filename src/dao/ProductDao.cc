#include "dao/ProductDao.h"

#include <cstdint>

namespace dao {

std::size_t ProductDao::deductStock(
    const std::shared_ptr<drogon::orm::Transaction>& tx,
    std::uint64_t productId,
    std::int64_t quantity) {

    // 防超卖：stock >= ? 的判定与扣减在同一条 UPDATE 内原子完成，
    // status = 1 排除下架商品。不能先 SELECT 再 UPDATE（TOCTOU），
    // 也不用 SELECT ... FOR UPDATE（间隙锁放大冲突）。version 自增只作辅助。
    // 注意：execSqlSync 返回 Result 值不是指针，用 `.` 访问。
    auto result = tx->execSqlSync(
        "UPDATE t_product"
        "   SET stock = stock - ?, version = version + 1, updated_at = NOW()"
        " WHERE id = ? AND stock >= ? AND status = 1",
        static_cast<long long>(quantity),
        static_cast<long long>(productId),
        static_cast<long long>(quantity));
    return result.affectedRows();
}

std::optional<models::Product> ProductDao::getById(
    const std::shared_ptr<drogon::orm::Transaction>& tx,
    std::uint64_t productId) {

    // DECIMAL 不经 double 中转：SQL 里 CAST 成「分」取回 int64。
    auto result = tx->execSqlSync(
        "SELECT id, product_no, name,"
        "       CAST(price * 100 AS SIGNED) AS price_fen, stock, status"
        "  FROM t_product"
        " WHERE id = ?",
        static_cast<long long>(productId));

    if (result.empty()) {
        return std::nullopt;
    }

    const auto& row = result[0];
    models::Product p;
    p.id        = static_cast<std::uint64_t>(row["id"].as<long long>());
    p.productNo = row["product_no"].as<std::string>();
    p.name      = row["name"].as<std::string>();
    p.priceFen  = row["price_fen"].as<long long>();
    p.stock     = row["stock"].as<long long>();
    p.status    = row["status"].as<int>();
    return p;
}

}  // namespace dao
