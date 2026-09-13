// 雪花算法 ID 生成器，生成订单号/支付单号/消息 ID 等业务主键。
// 单实例：datacenterId/workerId 固定 1，多实例时改成注入。
#pragma once

#include <string>

namespace utils {

class IdGenerator {
public:
    IdGenerator() = delete;

    // 生成 64 位雪花 ID，返回十进制字符串。线程安全。
    // 时钟回拨直接抛异常：继续产号有重复风险，宁可停服也别发重号。
    static std::string nextId();
};

}  // namespace utils
