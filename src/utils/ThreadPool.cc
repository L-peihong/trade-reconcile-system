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
    // 析构时未开始的任务直接丢弃。池里只跑请求级短任务，
    // 服务停止时丢掉可接受，客户端会超时重试。
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

        // 任务异常不能杀死工作线程，兜底记日志后继续。
        // 能走到这里说明业务代码有未捕获异常
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
