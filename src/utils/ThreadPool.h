// ============================================================================
// 固定大小工作线程池
//
// 为什么存在（CLAUDE.md 4.1 / 6.6）：
// Drogon 事件循环线程禁止调用同步 DB API —— fast 客户端下 execSqlSync 在
// 循环线程死锁、newTransaction() 直接 assert(0)。Service 层的同步事务
// 统一提交到本池执行：Controller handler 只 submit，不阻塞循环线程。
// ============================================================================
#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace utils {

class ThreadPool {
public:
    explicit ThreadPool(std::size_t threadCount = 8);
    ~ThreadPool();  // 停止接收任务并 join 所有工作线程

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // 提交任务。池已停止（析构中）时拒绝并返回 false。
    bool submit(std::function<void()> task);

    std::size_t threadCount() const noexcept { return threads_.size(); }

private:
    void workerLoop();

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> tasks_;
    bool stopped_ = false;
    std::vector<std::thread> threads_;
};

// 全局单例（8 线程）。main 里显式调用一次以预热（首次静态初始化也在 C++11
// 起线程安全，但显式调用让「池在服务开始前已就绪」成为确定事实）。
ThreadPool& globalThreadPool();

}  // namespace utils
