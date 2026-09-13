// ============================================================================
// 支付记录模型 + 回调请求 DTO（纯数据结构 —— CLAUDE.md 2）
// 金额一律「分」int64_t（CLAUDE.md 6.5）。
// ============================================================================
#pragma once

#include <cstdint>
#include <string>

namespace models {

// 支付记录（t_payment，CLAUDE.md 5.5）
struct Payment {
    enum class Status : int {
        kPending = 0,
        kSuccess = 1,
        kFailed  = 2,
    };

    std::string paymentNo;
    std::string orderNo;
    std::string channel;          // MOCK / ALIPAY / WECHAT（V1 只实现 MOCK）
    std::string channelTradeNo;   // 渠道交易号，uk_channel_trade 幂等键
    std::int64_t amountFen = 0;   // 渠道回调金额
    Status status = Status::kSuccess;
    std::string callbackTime;     // 渠道时间戳转换的北京时间（对账口径，CLAUDE.md 6.9）
    std::string rawBody;          // 回调原文，排查与对账举证
};

// 渠道回调请求 DTO（Controller 解析 → Service 消费）
// amount 为元字符串（签名协议的一部分，转换用 MoneyUtil::yuanStringToFen）。
struct PaymentCallbackReq {
    std::string requestId;        // X-Request-Id（流水幂等键）
    std::string orderNo;
    std::string channel;
    std::string channelTradeNo;
    std::string amount;
    std::string timestamp;        // 渠道秒级时间戳（字符串）
    std::string sign;
};

}  // namespace models
