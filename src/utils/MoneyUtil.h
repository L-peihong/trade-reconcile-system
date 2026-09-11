// ============================================================================
// 金额工具（CLAUDE.md 6.5）
//
// 应用层一律用 int64_t「分」运算，绝不经过 double。
// 与 MySQL DECIMAL(18,2)（元）的边界转换收敛在本文件：
//   - 写库：fenToYuanString() 生成 "399.00" 字符串，参数化绑定给 DECIMAL 列
//   - 读库：DAO 层 SQL 内 CAST(col * 100 AS SIGNED) 直接取分
//   - 展示：fenToYuanString() 输出到 JSON，前端拿到精确字符串
// ============================================================================
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace utils {

// 分 → 元字符串，恒两位小数："399.00" / "-12.50" / "0.05"
std::string fenToYuanString(std::int64_t fen);

// 元字符串 → 分。支持 "399.00" / "399" / "399.5" / "-12.50"。
// 非法输入（空串、非数字、超过两位小数、超过 10 亿元）返回 nullopt。
std::optional<std::int64_t> yuanStringToFen(const std::string& yuan);

}  // namespace utils
