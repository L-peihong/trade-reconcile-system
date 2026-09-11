// ============================================================================
// 金额工具单测 —— 元/分边界转换的正确性是「钱不能算错」的第一道防线
// （CLAUDE.md 6.5：金额一律整数分运算，不经 double）
// ============================================================================
#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>

#include "utils/MoneyUtil.h"

TEST(MoneyUtilTest, FenToYuanString) {
    EXPECT_EQ("399.00", utils::fenToYuanString(39900));
    EXPECT_EQ("0.05", utils::fenToYuanString(5));
    EXPECT_EQ("0.00", utils::fenToYuanString(0));
    EXPECT_EQ("-12.50", utils::fenToYuanString(-1250));
    EXPECT_EQ("100000000.00", utils::fenToYuanString(10000000000LL));
}

TEST(MoneyUtilTest, YuanStringToFen) {
    EXPECT_EQ(std::optional<std::int64_t>(39900), utils::yuanStringToFen("399.00"));
    EXPECT_EQ(std::optional<std::int64_t>(39900), utils::yuanStringToFen("399"));
    EXPECT_EQ(std::optional<std::int64_t>(39950), utils::yuanStringToFen("399.5"));
    EXPECT_EQ(std::optional<std::int64_t>(5), utils::yuanStringToFen("0.05"));
    EXPECT_EQ(std::optional<std::int64_t>(-1250), utils::yuanStringToFen("-12.50"));
    EXPECT_EQ(std::optional<std::int64_t>(1250), utils::yuanStringToFen("+12.50"));
}

TEST(MoneyUtilTest, YuanStringToFenRejectsGarbage) {
    EXPECT_EQ(std::nullopt, utils::yuanStringToFen(""));
    EXPECT_EQ(std::nullopt, utils::yuanStringToFen("abc"));
    EXPECT_EQ(std::nullopt, utils::yuanStringToFen("1.234"));   // 超过两位小数
    EXPECT_EQ(std::nullopt, utils::yuanStringToFen("12.3.4"));   // 两个小数点
    EXPECT_EQ(std::nullopt, utils::yuanStringToFen("."));
    EXPECT_EQ(std::nullopt, utils::yuanStringToFen("-"));
    EXPECT_EQ(std::nullopt, utils::yuanStringToFen("1e5"));      // 科学计数法不收
    EXPECT_EQ(std::nullopt, utils::yuanStringToFen(" 12"));      // 空白不收
}

TEST(MoneyUtilTest, RoundTrip) {
    // 双向转换往返一致：元串 → 分 → 元串
    const std::string yuan = "123456789.99";
    const auto fen = utils::yuanStringToFen(yuan);
    ASSERT_TRUE(fen.has_value());
    EXPECT_EQ(yuan, utils::fenToYuanString(*fen));
}
