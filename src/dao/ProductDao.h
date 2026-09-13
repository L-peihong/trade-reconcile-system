// 商品表 DAO，单表 CRUD，事务由 Service 传入。
// 无状态，线程安全。
#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include <drogon/orm/DbClient.h>

#include "models/Product.h"

namespace dao {

class ProductDao {
public:
    // 防超卖的条件扣减：判定与扣减合并成一条原子 UPDATE，
    // 靠 InnoDB 行锁 + 受影响行数。返回 0 = 库存不足或已下架，
    // 判定只看受影响行数，不靠补查结果。
    std::size_t deductStock(const std::shared_ptr<drogon::orm::Transaction>& tx,
                            std::uint64_t productId,
                            std::int64_t quantity);

    // 按主键查询（下单前校验存在性与上架状态、读取单价快照）
    std::optional<models::Product> getById(
        const std::shared_ptr<drogon::orm::Transaction>& tx,
        std::uint64_t productId);
};

}  // namespace dao
