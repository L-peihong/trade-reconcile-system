// 事务 RAII 封装，Service 层事务的唯一管理方式。
// 先记住 Drogon 1.9 的几个语义：
//   - 析构 = 自动 COMMIT，所以绝不能靠析构做回滚，guard 析构时未 finished 会显式 rollback 兜底。
//   - Transaction 没有 commit() 方法，显式提交走 execSqlSync("commit")：严格有序、同步阻塞、失败抛异常。
//   - rollback() 幂等，重复调用安全；事务内任一 SQL 失败会自动回滚，guard 的显式 rollback 只是兜底。
#pragma once

#include <drogon/orm/DbClient.h>

#include <memory>
#include <stdexcept>
#include <utility>

namespace utils {

// Service 层事务的唯一管理方式，别裸用事务指针做提交/回滚。
class TransactionGuard {
public:
    explicit TransactionGuard(std::shared_ptr<drogon::orm::Transaction> tx)
        : tx_(std::move(tx)) {
        if (!tx_) {
            throw std::invalid_argument("TransactionGuard: 空事务指针");
        }
    }

    ~TransactionGuard() {
        // 只做一件事：未显式提交就回滚
        if (tx_ && !finished_) {
            try {
                tx_->rollback();
            } catch (...) {
                // 析构不能抛；rollback 幂等，SQL 失败时 Drogon 已自动回滚过，这里吞掉
            }
        }
    }

    // 禁止拷贝与移动：guard 是栈上局部变量，负责权唯一
    TransactionGuard(const TransactionGuard&) = delete;
    TransactionGuard& operator=(const TransactionGuard&) = delete;
    TransactionGuard(TransactionGuard&&) = delete;
    TransactionGuard& operator=(TransactionGuard&&) = delete;

    // 显式提交。失败抛异常，上层转错误码，析构兜底回滚。
    void commit() {
        if (!tx_ || finished_) {
            throw std::logic_error("TransactionGuard: 重复提交或状态非法");
        }
        tx_->execSqlSync("commit");
        finished_ = true;
    }

    std::shared_ptr<drogon::orm::Transaction> tx() const { return tx_; }

private:
    std::shared_ptr<drogon::orm::Transaction> tx_;
    bool finished_ = false;
};

// 开启新事务。仅 Service 层调用，DAO 永不自行开。
// 两个前提，违反会死锁或 assert：
//   1. 不能在 Drogon 事件循环线程上调用，统一走工作线程池；
//   2. config.json 的 db_clients 必须 is_fast=false，同步事务接口只有慢客户端有。
std::shared_ptr<drogon::orm::Transaction> beginTransaction();

}  // namespace utils
