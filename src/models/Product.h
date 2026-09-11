// ============================================================================
// 商品领域模型（纯数据结构，无业务逻辑 —— CLAUDE.md 2）
// 金额一律「分」int64_t（CLAUDE.md 6.5），与 DECIMAL 的边界转换见 MoneyUtil。
// ============================================================================
#pragma once

#include <cstdint>
#include <string>

#include <jsoncpp/json/json.h>

#include "utils/MoneyUtil.h"

namespace models {

struct Product {
    std::uint64_t id        = 0;
    std::string   productNo;
    std::string   name;
    std::int64_t  priceFen  = 0;  // 单价，单位「分」
    std::int64_t  stock     = 0;
    int           status    = 0;  // 0 下架 1 上架

    bool onSale() const { return status == 1; }

    Json::Value toJson() const {
        Json::Value v;
        v["productId"] = static_cast<Json::UInt64>(id);
        v["productNo"] = productNo;
        v["name"]      = name;
        v["price"]     = utils::fenToYuanString(priceFen);
        v["stock"]     = static_cast<Json::Int64>(stock);
        v["status"]    = status;
        return v;
    }
};

}  // namespace models
