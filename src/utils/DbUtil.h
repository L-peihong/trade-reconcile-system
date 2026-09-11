// ============================================================================
// 事务 RAII 封装 —— CLAUDE.md 4.1 的契约实现，写事务代码前必读那一节
//
// 语义基础（已核对 Drogon v1.9.11 源码）：
//   - ~Transaction() = 自动 COMMIT（TransactionImpl.cc:35-86）。绝不依赖
//     析构做回滚 —— 那是隐式提交。guard 析构在未 finished 时显式 rollback 兜底。
//   - Transaction 没有 commit() 方法（头文件中被注释）。显式提交 =
//     execSqlSync("commit")，走事务自身 SQL 管线：严格有序、同步阻塞、
//     失败抛异常 —— 这是「提交失败必须抛异常」（4.1）的实现方式。
//   - rollback() 幂等（TransactionImpl.cc:158-159），重复调用安全。
//   - 事务内任一 SQL 失败 Drogon 自动 rollback（TransactionImpl.cc:124/232），
//     guard 的显式 rollback 是兜底，与自动回滚叠加安全。
// ============================================================================
#pragma once

#include <drogon/orm/DbClient.h>

#include <memory>
#include <stdexcept>
#include <utility>

namespace utils {

// Service 层事务的唯一管理方式，禁止裸用 TransactionPtr 做提交/回滚。
class TransactionGuard {
public:
    explicit TransactionGuard(std::shared_ptr<drogon::orm::Transaction> tx)
        : tx_(std::move(tx)) {
        if (!tx_) {
            throw std::invalid_argument("TransactionGuard: 空事务指针");
        }
    }

    ~TransactionGuard() {
        // 只做一件事：未显式提交 → rollback。
        if (tx_ && !finished_) {
            try {
                tx_->rollback();
            } catch (...) {
                // 析构不能抛；rollback 幂等且通常已自动回滚，
                // 这里吞掉是唯一允许的吞异常。
            }
        }
    }

    // 禁止拷贝与移动（修正 1）：同步场景下 guard 是栈上局部变量，
    // 负责权唯一，禁止移动。
    TransactionGuard(const TransactionGuard&) = delete;
    TransactionGuard& operator=(const TransactionGuard&) = delete;
    TransactionGuard(TransactionGuard&&) = delete;
    TransactionGuard& operator=(TransactionGuard&&) = delete;

    // 显式提交。失败抛异常 → 上层转错误码 → 析构兜底 rollback。
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

// 开启新事务。仅 Service 层调用，DAO 永不自行调用（CLAUDE.md 4.1）。
// 两个硬性前提（违反则死锁/assert，见 CLAUDE.md 4.1 / 6.6）：
//   1. 调用线程不是 Drogon 事件循环线程 —— 统一由工作线程池调用；
//   2. config.json 的 db_clients 必须 is_fast=false —— 同步事务接口只在
//      慢客户端（DbClientImpl）存在，fast 客户端直接 assert(0)。
std::shared_ptr<drogon::orm::Transaction> beginTransaction();

}  // namespace utils
