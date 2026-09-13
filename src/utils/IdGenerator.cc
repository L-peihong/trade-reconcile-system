#include "utils/IdGenerator.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <stdexcept>

namespace utils {
namespace {

// 64 位布局：1 符号 + 41 时间戳 + 5 datacenter + 5 worker + 12 序列
constexpr std::int64_t kEpochMs            = 1704067200000LL;  // 2024-01-01 00:00:00 UTC
constexpr std::int64_t kDatacenterIdBits   = 5;
constexpr std::int64_t kWorkerIdBits       = 5;
constexpr std::int64_t kSequenceBits       = 12;
constexpr std::int64_t kMaxSequence        = (1LL << kSequenceBits) - 1;
constexpr std::int64_t kTimestampShift     = kDatacenterIdBits + kWorkerIdBits + kSequenceBits;
constexpr std::int64_t kDatacenterShift    = kWorkerIdBits + kSequenceBits;
constexpr std::int64_t kWorkerShift        = kSequenceBits;
constexpr std::int64_t kDatacenterId       = 1;  // 单实例固定值
constexpr std::int64_t kWorkerId           = 1;

std::mutex gMutex;
std::int64_t gLastTimestampMs = -1;
std::int64_t gSequence        = 0;

std::int64_t currentTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// 同一毫秒内序列耗尽时，自旋等待下一个毫秒
std::int64_t waitNextMs(std::int64_t lastMs) {
    std::int64_t ts = currentTimeMs();
    while (ts <= lastMs) {
        ts = currentTimeMs();
    }
    return ts;
}

}  // namespace

std::string IdGenerator::nextId() {
    std::lock_guard<std::mutex> lock(gMutex);

    std::int64_t nowMs = currentTimeMs();
    if (nowMs < gLastTimestampMs) {
        throw std::runtime_error(
            "IdGenerator: system clock moved backwards, refusing to generate id");
    }

    if (nowMs == gLastTimestampMs) {
        gSequence = (gSequence + 1) & kMaxSequence;
        if (gSequence == 0) {
            nowMs = waitNextMs(gLastTimestampMs);
        }
    } else {
        gSequence = 0;
    }
    gLastTimestampMs = nowMs;

    const std::int64_t id =
        ((nowMs - kEpochMs) << kTimestampShift) |
        (kDatacenterId << kDatacenterShift) |
        (kWorkerId << kWorkerShift) |
        gSequence;

    return std::to_string(id);
}

}  // namespace utils
