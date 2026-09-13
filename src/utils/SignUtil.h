// ============================================================================
// 回调验签工具（亮点②支付侧的一半）
// Mock 渠道签名协议（与 scripts/mock_pay.sh 严格一致，改协议两处同改）：
//   canonical = orderNo|channelTradeNo|amount|timestamp
//   sign      = hex(HMAC-SHA256(secret, canonical))，小写十六进制
// ============================================================================
#pragma once

#include <string>

namespace utils {

// HMAC-SHA256，返回小写十六进制字符串
std::string hmacSha256Hex(const std::string& key, const std::string& data);

// 校验 Mock 渠道签名。用 CRYPTO_memcmp 常量时间比较，防时序侧信道。
// amount 为元字符串（与协议一致，如 "5999.00"），timestamp 为秒级时间戳字符串。
bool verifyMockSign(const std::string& secret,
                    const std::string& orderNo,
                    const std::string& channelTradeNo,
                    const std::string& amount,
                    const std::string& timestamp,
                    const std::string& sign);

}  // namespace utils
