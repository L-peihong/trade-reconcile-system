// ============================================================================
// 统一返回体（CLAUDE.md 4.3）
//
// 所有 HTTP 接口的响应都是这个形状，无例外：
//   {"code":0,"message":"成功","data":{...},"requestId":"..."}
//   {"code":30002,"message":"库存不足","data":null,"requestId":"..."}
//
// 约定（CLAUDE.md 4.5）：统一用 Result<T> 表达「成功或业务失败」，
// 不用 -1 / 空字符串之类哨兵值，不用裸 bool。
// ============================================================================
#pragma once

#include <optional>
#include <string>
#include <type_traits>
#include <utility>

// Ubuntu 上 jsoncpp 的头在 /usr/include/jsoncpp/json/。
// 这里写全 <jsoncpp/json/json.h> 而不是 <json/json.h>：前者不依赖 CMake 是否正确
// 传递了 JSONCPP 的 include 目录，任何环境下都能解析；后者要靠 Jsoncpp::jsoncpp
// 目标把 /usr/include/jsoncpp 带进来，少一环就报 file not found。
#include <jsoncpp/json/json.h>

#include "common/ErrorCode.h"

namespace models {

namespace detail {

// 探测 T 是否自带 toJson()。领域模型（Order / Account / Payment ...）都会实现它，
// 只有它们才需要走这条分支。
template <typename T, typename = void>
struct has_to_json : std::false_type {};

template <typename T>
struct has_to_json<T, std::void_t<decltype(std::declval<const T&>().toJson())>>
    : std::true_type {};

// 把领域对象转成 Json::Value：
//   - 有 toJson() 的走它自己实现
//   - 其余（int/double/bool/std::string/Json::Value 本身）直接用 jsoncpp 的构造函数
//
// if constexpr 保证被丢弃的那个分支对本类型**不实例化** —— 所以拿一个既没有
// toJson() 又不被 jsoncpp 支持的类型去调 toJson()，报错点会落在这一行，
// 也就是"这个类型不能直接当 data 用"，定位起来比模板展开的几十行报错清爽得多。
template <typename T>
Json::Value toJsonValue(const T& value) {
    if constexpr (has_to_json<T>::value) {
        return value.toJson();
    } else {
        return Json::Value(value);
    }
}

}  // namespace detail

template <typename T>
struct Result {
    int                  code{static_cast<int>(common::ErrCode::kSuccess)};
    std::string          message;
    std::optional<T>     data;
    std::string          requestId;

    // ------------------------------------------------------------------------
    // 成功
    // ------------------------------------------------------------------------
    static Result ok(T value) {
        return makeOk(std::move(value), std::string{});
    }

    static Result ok(T value, std::string requestId) {
        return makeOk(std::move(value), std::move(requestId));
    }

    // ------------------------------------------------------------------------
    // 失败
    // message 传业务可读的原因；技术细节写日志，别塞这里（CLAUDE.md 4.3）。
    // ------------------------------------------------------------------------
    static Result fail(common::ErrCode code, std::string message, std::string requestId = {}) {
        Result r;
        r.code      = static_cast<int>(code);
        r.message   = std::move(message);
        r.data      = std::nullopt;
        r.requestId = std::move(requestId);
        return r;
    }

    bool isOk() const noexcept {
        return code == static_cast<int>(common::ErrCode::kSuccess);
    }

    // ------------------------------------------------------------------------
    // 序列化。字段顺序与结构固定，前端与测试都依赖它。
    // ------------------------------------------------------------------------
    Json::Value toJson() const {
        Json::Value root;
        root["code"]      = code;
        root["message"]   = message;
        root["requestId"] = requestId;
        if (data.has_value()) {
            root["data"] = detail::toJsonValue(*data);
        } else {
            // 显式给 null，而不是让字段消失 —— 客户端可以无脑取 data，
            // 少写一层"字段存不存在"的判断。
            root["data"] = Json::Value(Json::nullValue);
        }
        return root;
    }

private:
    static Result makeOk(T value, std::string requestId) {
        Result r;
        r.code      = static_cast<int>(common::ErrCode::kSuccess);
        r.message   = common::toMessage(common::ErrCode::kSuccess);
        r.data      = std::move(value);
        r.requestId = std::move(requestId);
        return r;
    }
};

}  // namespace models
