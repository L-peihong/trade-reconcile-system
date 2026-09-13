// 账户表 + 流水表 DAO，单表 CRUD，事务由 Service 传入。
// 余额扣减走条件 UPDATE，和防超卖同一个套路。
#pragma once

#include <memory>
#include <optional>

#include <drogon/orm/DbClient.h>

#include "models/Account.h"

namespace dao {

class AccountDao {
public:
    // 按用户 ID 查询账户（V1：一个用户一个账户）
    std::optional<models::Account> getByUserId(
        const std::shared_ptr<drogon::orm::Transaction>& tx,
        std::uint64_t userId);

    // 条件扣减余额：判定与扣减合并成一条原子 UPDATE。
    // 返回 0 = 余额不足或账户非正常状态 —— 判定依据永远是 affectedRows。
    std::size_t deduct(const std::shared_ptr<drogon::orm::Transaction>& tx,
                       const std::string& accountNo,
                       std::int64_t amountFen);

    // 写流水（只增不改）。before/after 由 Service 扣减成功后回读余额算出：
    // before = after + amount 是确定关系，没有并发读脏问题。
    void insertFlow(const std::shared_ptr<drogon::orm::Transaction>& tx,
                    const models::AccountFlow& flow);
};

}  // namespace dao
