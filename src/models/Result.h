// 统一返回体，所有 HTTP 接口的响应都是这个形状，无例外：
// {"code":0,"message":"成功","data":{...},"requestId":"..."}
// 业务失败统一用 Result<T> 表达，不用 -1 / 空字符串之类哨兵值。
#pragma once

#include <optional>
#include <string>
#include <type_traits>
#include <utility>

// 写全 <jsoncpp/json/json.h>，不依赖 CMake 是否正确传递 jsoncpp 的 include 目录。
#include <jsoncpp/json/json.h>

#include "common/ErrorCode.h"

namespace models {

namespace detail {

// 探测 T 是否自带 toJson()。领域模型都会实现它，只有它们走这条分支。
template <typename T, typename = void>
struct has_to_json : std::false_type {};

template <typename T>
struct has_to_json<T, std::void_t<decltype(std::declval<const T&>().toJson())>>
    : std::true_type {};

// 有 toJson() 的走它自己实现，其余直接用 jsoncpp 构造函数。
// if constexpr 保证弃用的分支不实例化：不支持的类型报错点会落在这里，
// 比模板展开几十行报错好定位。
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

    // 成功
    static Result ok(T value) {
        return makeOk(std::move(value), std::string{});
    }

    static Result ok(T value, std::string requestId) {
        return makeOk(std::move(value), std::move(requestId));
    }

    // 失败。message 只给业务可读的原因，技术细节进日志。
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

    // 序列化。字段顺序与结构固定，前端和测试都依赖。
    Json::Value toJson() const {
        Json::Value root;
        root["code"]      = code;
        root["message"]   = message;
        root["requestId"] = requestId;
        if (data.has_value()) {
            root["data"] = detail::toJsonValue(*data);
        } else {
            // 显式给 null 而不是让字段消失，客户端少写一层存在性判断。
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
