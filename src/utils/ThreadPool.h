// 固定大小工作线程池。
// Drogon 事件循环线程不能跑同步 DB API（fast 客户端下 execSqlSync 死锁、
// newTransaction 直接 assert），Service 的同步事务统一丢到这个池里执行，
// Controller handler 只 submit，不阻塞循环线程。
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
    ~ThreadPool();  // 停止收任务并 join 所有线程

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

// 全局单例（8 线程）。main 里显式调用一次预热，保证服务开始前池已就绪。
ThreadPool& globalThreadPool();

}  // namespace utils
