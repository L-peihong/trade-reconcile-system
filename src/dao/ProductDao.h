// ============================================================================
// 商品表 DAO —— 单表 CRUD，不开事务（CLAUDE.md 4.1）
// 事务对象由 Service 显式传入；本类无状态，可被多线程并发使用。
// ============================================================================
#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include <drogon/orm/DbClient.h>

#include "models/Product.h"

namespace dao {

class ProductDao {
public:
    // 条件扣减库存：防超卖的核心（CLAUDE.md 6.1）。
    // 把「判定」与「扣减」合并成一条原子 UPDATE，靠 InnoDB 行锁 + 受影响行数。
    // 返回 0 表示库存不足或已下架 —— 判定依据永远是它，不是补查结果。
    std::size_t deductStock(const std::shared_ptr<drogon::orm::Transaction>& tx,
                            std::uint64_t productId,
                            std::int64_t quantity);

    // 按主键查询（下单前校验存在性与上架状态、读取单价快照）
    std::optional<models::Product> getById(
        const std::shared_ptr<drogon::orm::Transaction>& tx,
        std::uint64_t productId);
};

}  // namespace dao
