// 订单领域模型 + 下单请求 DTO，纯数据结构。
// 状态机单向禁止回退；金额一律「分」int64_t。
#pragma once

#include <cstdint>
#include <string>

#include <jsoncpp/json/json.h>

#include "utils/MoneyUtil.h"

namespace models {

// 状态机：实现 0→1→2 与 0→3；1→4 退款留作后续，状态值先占位。
enum class OrderStatus : int {
    kPendingPayment = 0,  // 待支付
    kPaid           = 1,  // 已支付
    kCompleted      = 2,  // 已完成
    kCancelled      = 3,  // 已取消（超时关单，归还库存）
    kRefunded       = 4,  // 已退款（预留）
};

struct Order {
    std::string orderNo;
    std::string requestId;     // 创建请求 ID（幂等兜底，uk_request_id）
    std::uint64_t userId   = 0;
    std::uint64_t productId = 0;
    std::int64_t quantity  = 0;
    std::int64_t unitPriceFen  = 0;  // 下单时快照单价，不跟随改价
    std::int64_t totalAmountFen = 0;  // = unitPriceFen * quantity
    OrderStatus status = OrderStatus::kPendingPayment;
    // 支付超时时间 "YYYY-MM-DD HH:MM:SS"，应用侧算好随 INSERT 写入。
    std::string expireTime;
    std::string paidAt;  // 空 = 未支付

    Json::Value toJson() const {
        Json::Value v;
        v["orderNo"]     = orderNo;
        v["userId"]      = static_cast<Json::UInt64>(userId);
        v["productId"]   = static_cast<Json::UInt64>(productId);
        v["quantity"]    = static_cast<Json::Int64>(quantity);
        v["unitPrice"]   = utils::fenToYuanString(unitPriceFen);
        v["totalAmount"] = utils::fenToYuanString(totalAmountFen);
        v["status"]      = static_cast<int>(status);
        v["expireTime"]  = expireTime;
        return v;
    }
};

// 下单请求 DTO（Controller 解析 → Service 消费）
struct CreateOrderReq {
    std::string requestId;
    std::string requestHash;  // 请求体 SHA-256（幂等表「同 ID 异内容」检测）
    std::uint64_t userId   = 0;
    std::uint64_t productId = 0;
    std::int64_t quantity  = 0;
};

}  // namespace models
