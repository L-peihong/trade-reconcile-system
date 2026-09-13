// 订单表 DAO，单表 CRUD，事务由 Service 传入。
#pragma once

#include <memory>
#include <optional>
#include <string>

#include <drogon/orm/DbClient.h>

#include "models/Order.h"

namespace dao {

class OrderDao {
public:
    // 插入订单。expire_time 应用侧算好随行写入，插入后不回读。
    void insert(const std::shared_ptr<drogon::orm::Transaction>& tx,
                const models::Order& order);

    // 按订单号查询，支付回调用。
    std::optional<models::Order> getByOrderNo(
        const std::shared_ptr<drogon::orm::Transaction>& tx,
        const std::string& orderNo);

    // 状态流转：待支付 → 已支付。带原状态条件 WHERE status = 0，
    // 靠受影响行数判断是否被抢先改过；返回 0 = 不是待支付状态，由 Service 补查定语义。
    std::size_t markPaid(const std::shared_ptr<drogon::orm::Transaction>& tx,
                         const std::string& orderNo);
};

}  // namespace dao
