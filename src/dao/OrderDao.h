// ============================================================================
// 订单表 DAO —— 单表 CRUD，不开事务（CLAUDE.md 4.1）
// ============================================================================
#pragma once

#include <memory>
#include <optional>
#include <string>

#include <drogon/orm/DbClient.h>

#include "models/Order.h"

namespace dao {

class OrderDao {
public:
    // 插入订单。expire_time 由应用侧算好随行写入（修正 3），
    // 插入后不回读 —— 提交成功直接返回内存对象。
    void insert(const std::shared_ptr<drogon::orm::Transaction>& tx,
                const models::Order& order);

    // 按订单号查询。链路③（支付回调）使用。
    std::optional<models::Order> getByOrderNo(
        const std::shared_ptr<drogon::orm::Transaction>& tx,
        const std::string& orderNo);

    // 订单状态流转：待支付 → 已支付（CLAUDE.md 5.4 状态机，单向禁止回退）。
    // 带原状态条件：WHERE status = 0，靠 affectedRows 判断是否被抢先改过。
    // 返回 0 = 订单不是待支付状态（已支付/已取消），由 Service 补查定语义。
    std::size_t markPaid(const std::shared_ptr<drogon::orm::Transaction>& tx,
                         const std::string& orderNo);
};

}  // namespace dao
