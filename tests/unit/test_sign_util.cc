// ============================================================================
// 回调验签单测 —— HMAC 正确性与签名协议一致性是支付链路的信任根基
// ============================================================================
#include <gtest/gtest.h>

#include <string>

#include "utils/SignUtil.h"

TEST(SignUtilTest, HmacSha256KnownVector) {
    // RFC 4231 标准测试向量：key="key", data="The quick brown fox jumps over the lazy dog"
    const std::string expected =
        "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8";
    EXPECT_EQ(expected,
              utils::hmacSha256Hex("key",
                                   "The quick brown fox jumps over the lazy dog"));
}

TEST(SignUtilTest, VerifyMockSignRoundTrip) {
    const std::string secret = "mock-channel-secret-2026";
    const std::string orderNo = "357367931212009472";
    const std::string tradeNo = "MOCK1752800000";
    const std::string amount  = "5999.00";
    const std::string ts      = "1752800000";

    // 按协议拼 canonical 再签名 → 验签必须通过
    const std::string canonical = orderNo + "|" + tradeNo + "|" + amount + "|" + ts;
    const std::string sign = utils::hmacSha256Hex(secret, canonical);

    EXPECT_TRUE(utils::verifyMockSign(secret, orderNo, tradeNo, amount, ts, sign));
}

TEST(SignUtilTest, VerifyMockSignRejectsTampering) {
    const std::string secret = "mock-channel-secret-2026";
    const std::string orderNo = "357367931212009472";
    const std::string tradeNo = "MOCK1752800000";
    const std::string amount  = "5999.00";
    const std::string ts      = "1752800000";

    const std::string canonical = orderNo + "|" + tradeNo + "|" + amount + "|" + ts;
    const std::string sign = utils::hmacSha256Hex(secret, canonical);

    // 篡改金额 → 拒
    EXPECT_FALSE(utils::verifyMockSign(secret, orderNo, tradeNo, "1.00", ts, sign));
    // 篡改订单号 → 拒
    EXPECT_FALSE(utils::verifyMockSign(secret, "999999999999999999", tradeNo, amount, ts, sign));
    // 换密钥 → 拒
    EXPECT_FALSE(utils::verifyMockSign("wrong-secret", orderNo, tradeNo, amount, ts, sign));
    // 签名长度错误 → 拒
    EXPECT_FALSE(utils::verifyMockSign(secret, orderNo, tradeNo, amount, ts, "abcd"));
}
