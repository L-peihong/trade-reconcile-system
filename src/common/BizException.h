// 业务异常。Service 层业务规则不满足时抛出，上层统一捕获转成 Result<T>。
// 只有业务失败才用它，数据库连不上这类意外交给标准异常。
// 异常能把失败直接穿过嵌套调用栈，顺带让 TransactionGuard 的析构回滚生效。
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

    // noexcept 不能漏：基类签名带 noexcept，覆写更宽松会编译失败。
    // 返回 message_ 内部指针，异常对象存活期间有效。
    const char* what() const noexcept override {
        return message_.c_str();
    }

    ErrCode code() const noexcept {
        return code_;
    }

    // 整型错误码，直接塞 Result::code。
    int intCode() const noexcept {
        return static_cast<int>(code_);
    }

private:
    ErrCode     code_;
    std::string message_;
};

}  // namespace common
