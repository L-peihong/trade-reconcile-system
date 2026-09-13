// 雪花 ID 单测：唯一性/递增性/线程安全是幂等与对账的基石
#include <gtest/gtest.h>

#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "utils/IdGenerator.h"

TEST(IdGeneratorTest, FormatConstraints) {
    const std::string id = utils::IdGenerator::nextId();
    EXPECT_FALSE(id.empty());
    EXPECT_LE(id.size(), 19);  // 64 位十进制上限 20 位，雪花首位为 0，最多 19 位
    for (const char c : id) {
        EXPECT_TRUE(c >= '0' && c <= '9') << "雪花 ID 必须是纯数字: " << id;
    }
}

TEST(IdGeneratorTest, MonotonicAndUnique) {
    std::set<std::string> ids;
    std::string prev = utils::IdGenerator::nextId();
    ids.insert(prev);
    for (int i = 0; i < 10000; ++i) {
        const std::string id = utils::IdGenerator::nextId();
        EXPECT_TRUE(id > prev) << "ID 必须严格递增";
        EXPECT_TRUE(ids.insert(id).second) << "ID 必须唯一";
        prev = id;
    }
}

TEST(IdGeneratorTest, ThreadSafeUniqueness) {
    constexpr int kThreads    = 8;
    constexpr int kPerThread  = 5000;

    std::mutex mutex;
    std::set<std::string> all;

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&all, &mutex]() {
            for (int i = 0; i < kPerThread; ++i) {
                const std::string id = utils::IdGenerator::nextId();
                std::lock_guard<std::mutex> lock(mutex);
                all.insert(id);
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    // 8 线程并发产号，若互斥失效必有碰撞
    EXPECT_EQ(all.size(), static_cast<std::size_t>(kThreads) * kPerThread);
}
