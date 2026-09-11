#include "utils/ThreadPool.h"

#include <utility>

#include "common/Logger.h"

namespace utils {

ThreadPool::ThreadPool(std::size_t threadCount) {
    threads_.reserve(threadCount);
    for (std::size_t i = 0; i < threadCount; ++i) {
        threads_.emplace_back([this]() { workerLoop(); });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
    }
    cv_.notify_all();
    for (auto& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    // 注意：析构时队列里未开始的任务被直接丢弃。本池只跑「请求级」短任务，
    // 服务停止时丢弃未开始的任务可接受 —— 客户端会超时重试。
}

bool ThreadPool::submit(std::function<void()> task) {
    if (!task) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_) {
            return false;
        }
        tasks_.push_back(std::move(task));
    }
    cv_.notify_one();
    return true;
}

void ThreadPool::workerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() { return stopped_ || !tasks_.empty(); });
            if (stopped_ && tasks_.empty()) {
                return;
            }
            task = std::move(tasks_.front());
            tasks_.pop_front();
        }

        // 任务异常不允许杀死工作线程，兜底记录后继续。
        // 走到这里说明业务代码有未捕获异常 —— 属于 bug，记 error。
        try {
            task();
        } catch (const std::exception& e) {
            if (auto log = common::Logger::get()) {
                log->error("thread pool task threw: {}", e.what());
            }
        } catch (...) {
            if (auto log = common::Logger::get()) {
                log->error("thread pool task threw non-std exception");
            }
        }
    }
}

ThreadPool& globalThreadPool() {
    static ThreadPool pool(8);
    return pool;
}

}  // namespace utils
