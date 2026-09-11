// ============================================================================
// 雪花算法 ID 生成器
//
// 用于生成订单号/支付单号/消息 ID 等业务主键（VARCHAR(32) 可容纳 19 位十进制）。
// 单实例 MVP：datacenterId / workerId 固定为 1；多实例化时改为注入。
// ============================================================================
#pragma once

#include <string>

namespace utils {

class IdGenerator {
public:
    IdGenerator() = delete;

    // 生成 64 位雪花 ID，返回十进制字符串。
    // 线程安全。时钟回拨时抛 std::runtime_error —— 交易系统宁停勿错：
    // 回拨期间继续产号有重复风险，比停服几分钟严重得多。
    static std::string nextId();
};

}  // namespace utils
