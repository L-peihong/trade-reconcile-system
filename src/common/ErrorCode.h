// ============================================================================
// 错误码定义（CLAUDE.md 4.3）
//
// 规则：5 位数字，AABBB 结构 —— 前 2 位是模块，后 3 位是模块内序号；0 表示成功。
//   10xxx 通用/参数   20xxx 账户   30xxx 商品/库存   40xxx 订单
//   50xxx 支付        60xxx 消息   70xxx 对账        90xxx 系统
//
// 本文件是错误码的**唯一事实来源**。业务代码里禁止写裸数字，
// 一律 via ErrCode::kXxx；新增错误码时同步更新 CLAUDE.md 5.x 的错误码表。
// ============================================================================
#pragma once

namespace common {

enum class ErrCode : int {
    // --- 成功 ---
    kSuccess = 0,

    // --- 10xxx 通用 / 参数 ---
    kParamInvalid     = 10001,  // 参数校验失败
    kMissingRequestId = 10002,  // 缺少或非法的 X-Request-Id（见 CLAUDE.md 4.2）
    kDuplicateRequest = 10003,  // 重复请求：幂等命中，业务尚未完成，客户端应稍后重试
    kNotFound         = 10004,  // 资源不存在

    // --- 20xxx 账户 ---
    kAccountNotFound  = 20001,  // 账户不存在
    kBalanceNotEnough = 20002,  // 余额不足

    // --- 30xxx 商品 / 库存 ---
    kProductNotFound = 30001,  // 商品不存在
    kStockNotEnough  = 30002,  // 库存不足（防超卖条件 UPDATE 的 affectedRows == 0）

    // --- 40xxx 订单 ---
    kOrderNotFound      = 40001,  // 订单不存在
    kOrderStatusInvalid = 40002,  // 订单状态不允许此操作（状态机单向，禁止回退）

    // --- 50xxx 支付 ---
    kSignVerifyFailed    = 50001,  // 渠道回调验签失败
    kAmountMismatch      = 50002,  // 回调金额与订单金额不一致
    kChannelTradeConflict = 50003, // 渠道交易号已绑定其他订单
    kUnsupportedChannel  = 50004,  // 不支持的渠道

    // --- 90xxx 系统 ---
    kDbError       = 90001,  // 数据库错误
    kInternalError = 90003,  // 内部未知异常（兜底）
};

// 返回面向调用方可读的错误描述。
//
// 注意（CLAUDE.md 4.3）：这里只放**业务语义**，不要把 SQL、表名、堆栈等
// 技术细节塞进来 —— 那类信息写日志，返回给客户端的 message 只讲人能看懂的原因。
//
// 定义在头文件里用 inline，避免为一个纯查表函数单开一个 .cc。
inline const char* toMessage(ErrCode code) noexcept {
    switch (code) {
        case ErrCode::kSuccess:
            return "成功";
        case ErrCode::kParamInvalid:
            return "参数校验失败";
        case ErrCode::kMissingRequestId:
            return "缺少或非法的 X-Request-Id";
        case ErrCode::kDuplicateRequest:
            return "重复请求，请稍后重试";
        case ErrCode::kNotFound:
            return "资源不存在";
        case ErrCode::kAccountNotFound:
            return "账户不存在";
        case ErrCode::kBalanceNotEnough:
            return "余额不足";
        case ErrCode::kProductNotFound:
            return "商品不存在";
        case ErrCode::kStockNotEnough:
            return "库存不足";
        case ErrCode::kOrderNotFound:
            return "订单不存在";
        case ErrCode::kOrderStatusInvalid:
            return "订单状态不允许此操作";
        case ErrCode::kSignVerifyFailed:
            return "签名校验失败";
        case ErrCode::kAmountMismatch:
            return "支付金额与订单金额不一致";
        case ErrCode::kChannelTradeConflict:
            return "渠道交易号已绑定其他订单";
        case ErrCode::kUnsupportedChannel:
            return "不支持的支付渠道";
        case ErrCode::kDbError:
            return "系统繁忙，请稍后重试";
        case ErrCode::kInternalError:
            return "系统内部错误";
        default:
            // CLAUDE.md 4.5：switch 覆盖枚举必须带 default。
            // 走到这里说明新增了错误码却忘了加 case，返回值刻意保持通用，
            // 免得把"未知的错误码"这种实现细节暴露给调用方。
            return "未知错误";
    }
}

// 便捷判定。写成函数而不是宏，便于在模板和常量表达式里使用。
inline bool isSuccess(ErrCode code) noexcept {
    return code == ErrCode::kSuccess;
}

}  // namespace common
