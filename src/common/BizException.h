// ============================================================================
// 业务异常
//
// 用途（CLAUDE.md 4.1 / 4.5）：Service 层遇到业务规则不满足时抛出，
// 由上层统一捕获并转成 Result<T>。异常只用于「业务失败」这一种确定性的分支，
// 真正的意外（数据库连不上、内存分配失败）交给标准异常。
//
// 为什么要有它：Service 里嵌套好几层调用，靠返回值逐层传递错误码会写成一堆
// if-return，且极易漏判；异常能把「失败」自然地穿过调用栈，让事务的 RAII
// 回滚自动生效。
// ============================================================================
#pragma once

#include <exception>
#include <string>
#include <utility>

#include "common/ErrorCode.h"

namespace common {

class BizException : public std::exception {
public:
    BizException(ErrCode code, std::string message)
        : code_(code), message_(std::move(message)) {}

    // 重写 what()。
    // 注意 noexcept：基类签名就是 noexcept，这里漏了会在 C++17 下直接编译失败
    // （覆写函数的异常规格不能比基类更宽松）。
    // 返回 message_ 内部的指针，只要异常对象还活着就有效 —— catch 块内一定安全。
    const char* what() const noexcept override {
        return message_.c_str();
    }

    ErrCode code() const noexcept {
        return code_;
    }

    // 取整型错误码，方便直接塞进 Result::code。
    int intCode() const noexcept {
        return static_cast<int>(code_);
    }

private:
    ErrCode     code_;
    std::string message_;
};

}  // namespace common
