#include "utils/SignUtil.h"

#include <openssl/crypto.h>
#include <openssl/hmac.h>

#include "common/Logger.h"

namespace utils {

std::string hmacSha256Hex(const std::string& key, const std::string& data) {
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    // HMAC 一次性接口在 OpenSSL 3 仍可用（标记弃用但不删），MVP 足够；
    // 升级时换 EVP_MAC 系列即可，输出字节不变。
    HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(data.data()), data.size(),
         md, &len);

    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (unsigned int i = 0; i < len; ++i) {
        out.push_back(kHex[md[i] >> 4]);
        out.push_back(kHex[md[i] & 0x0F]);
    }
    return out;
}

bool verifyMockSign(const std::string& secret,
                    const std::string& orderNo,
                    const std::string& channelTradeNo,
                    const std::string& amount,
                    const std::string& timestamp,
                    const std::string& sign) {
    const std::string canonical =
        orderNo + "|" + channelTradeNo + "|" + amount + "|" + timestamp;
    const std::string expected = hmacSha256Hex(secret, canonical);

    // 长度不等直接拒；长度相等时用常量时间比较。
    if (expected.size() != sign.size()) {
        if (auto log = common::Logger::get()) {
            log->warn("mock sign length mismatch: canonical=[{}] expected_len={} got_len={}",
                      canonical, expected.size(), sign.size());
        }
        return false;
    }
    const bool ok = CRYPTO_memcmp(expected.data(), sign.data(), expected.size()) == 0;
    if (!ok) {
        // 排障日志：把服务端实际参与的 canonical 与期望签名打出来，
        // 与 scripts/mock_pay.sh 的算法逐字段对账。
        if (auto log = common::Logger::get()) {
            log->warn("mock sign mismatch: canonical=[{}] expected=[{}] got=[{}] secret_len={}",
                      canonical, expected, sign, secret.size());
        }
    }
    return ok;
}

}  // namespace utils
