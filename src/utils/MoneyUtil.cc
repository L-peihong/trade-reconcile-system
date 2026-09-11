#include "utils/MoneyUtil.h"

#include <cstdint>

namespace utils {

std::string fenToYuanString(std::int64_t fen) {
    const bool negative = fen < 0;
    // 防 INT64_MIN 取负溢出：(fen+1) 再取负再 +1
    const std::uint64_t abs = negative
        ? static_cast<std::uint64_t>(-(fen + 1)) + 1
        : static_cast<std::uint64_t>(fen);

    const std::uint64_t yuan = abs / 100;
    const std::uint64_t rem  = abs % 100;

    return (negative ? "-" : "") + std::to_string(yuan) + "." +
           (rem < 10 ? "0" : "") + std::to_string(rem);
}

std::optional<std::int64_t> yuanStringToFen(const std::string& raw) {
    if (raw.empty() || raw.size() > 20) {
        return std::nullopt;
    }

    std::size_t i = 0;
    bool negative = false;
    if (raw[i] == '+' || raw[i] == '-') {
        negative = (raw[i] == '-');
        ++i;
    }

    std::int64_t yuan = 0;
    std::int64_t frac = 0;
    int fracDigits = 0;
    bool seenDot = false;
    bool seenDigit = false;

    for (; i < raw.size(); ++i) {
        const char c = raw[i];
        if (c == '.') {
            if (seenDot) {
                return std::nullopt;  // 第二个小数点
            }
            seenDot = true;
            continue;
        }
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        seenDigit = true;
        const int d = c - '0';
        if (!seenDot) {
            if (yuan > 100000000LL) {
                return std::nullopt;  // 上限 10 亿元，排除溢出路径
            }
            yuan = yuan * 10 + d;
        } else {
            if (++fracDigits > 2) {
                return std::nullopt;  // 最多两位小数
            }
            frac = frac * 10 + d;
        }
    }

    if (!seenDigit) {
        return std::nullopt;  // "" / "-" / "." 之类
    }
    if (fracDigits == 1) {
        frac *= 10;  // "399.5" → 39950
    }

    const std::int64_t fen = yuan * 100 + frac;
    return negative ? -fen : fen;
}

}  // namespace utils
