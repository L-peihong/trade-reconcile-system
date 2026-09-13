// 账户与流水模型，纯数据结构。
// 金额一律「分」int64_t。当前只做入账/出账，冻结解冻留作后续。
#pragma once

#include <cstdint>
#include <string>

namespace models {

// 账户（t_account）
struct Account {
    std::string accountNo;
    std::uint64_t userId = 0;
    std::int64_t balanceFen = 0;        // 可用余额
    std::int64_t frozenAmountFen = 0;   // 冻结金额（预留）
    int status = 0;                     // 0 正常 1 冻结 2 注销
};

// 流水业务类型（t_account_flow.biz_type）
enum class FlowBizType : int {
    kRecharge = 1,  // 充值
    kPayment  = 2,  // 支付
    kRefund   = 3,  // 退款（预留）
    kFreeze   = 4,  // 冻结（预留）
    kUnfreeze = 5,  // 解冻（预留）
};

// 流水方向（t_account_flow.direction）
enum class FlowDirection : int {
    kIn  = 1,  // 入账
    kOut = 2,  // 出账
};

// 账户流水（t_account_flow，只增不改，对账的本地数据源）
struct AccountFlow {
    std::string accountNo;
    FlowBizType bizType = FlowBizType::kPayment;
    FlowDirection direction = FlowDirection::kOut;
    std::int64_t amountFen = 0;      // 恒为正，方向由 direction 表达
    std::int64_t beforeFen = 0;      // 变动前余额
    std::int64_t afterFen = 0;       // 变动后余额
    std::string orderNo;             // 关联订单号
    std::string requestId;           // 来源请求 ID（uk_request_biz 防重复记账）
};

}  // namespace models
