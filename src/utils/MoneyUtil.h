// 金额工具。应用层一律用 int64_t 分运算，不过 double。
// 与 MySQL DECIMAL(18,2)（元）的边界转换收在这里：写库绑定元字符串、
// 读库 SQL 里 CAST(col*100 AS SIGNED) 取分、展示转元字符串。
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace utils {

// 分 → 元字符串，恒两位小数："399.00" / "-12.50" / "0.05"
std::string fenToYuanString(std::int64_t fen);

// 元字符串 → 分，支持 "399.00" / "399" / "399.5" / "-12.50"。
// 非法输入（空串、非数字、超过两位小数、超过 10 亿元）返回 nullopt。
std::optional<std::int64_t> yuanStringToFen(const std::string& yuan);

}  // namespace utils
