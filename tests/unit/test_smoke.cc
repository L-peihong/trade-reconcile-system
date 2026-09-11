// ============================================================================
// 冒烟测试
//
// 存在的意义是回答一个问题：**这套构建链路是通的吗？**
// 能编译、能链接 trade_core、能跑起来、ctest 能发现它。
//
// 后续随业务铺开，测试放到 tests/unit/test_order_service.cc 等文件里，
// 本文件保留不动 —— 构建出问题时它能最快把"是构建坏了还是业务写错了"区分开。
// ============================================================================
#include <gtest/gtest.h>

#include <string>

#include "common/BizException.h"
#include "common/ErrorCode.h"
#include "models/Result.h"

// ---------------------------------------------------------------------------
// 你要求的那个 trivial 用例。
//
// ⚠️ 它本身**不能**验证 trade_core 链接是否正确：静态库只有在符号被引用时才会
//    被链接器拉进来，没人引用就等于没链，链接错误自然也暴露不出来。
//    所以下面还有三个真正调用 trade_core 代码的用例 —— 它们才是这条链路的自检。
// ---------------------------------------------------------------------------
TEST(SmokeTest, Trivial) {
    EXPECT_EQ(1, 1);
}

// ---------------------------------------------------------------------------
// 错误码：既验数值（协议契约，改了就破坏兼容），也验文案。
// ---------------------------------------------------------------------------
TEST(SmokeTest, ErrorCodeValues) {
    EXPECT_EQ(0, static_cast<int>(common::ErrCode::kSuccess));
    EXPECT_EQ(10002, static_cast<int>(common::ErrCode::kMissingRequestId));
    EXPECT_EQ(30002, static_cast<int>(common::ErrCode::kStockNotEnough));
    EXPECT_EQ(90003, static_cast<int>(common::ErrCode::kInternalError));
}

TEST(SmokeTest, ErrorCodeMessages) {
    EXPECT_STREQ("成功", common::toMessage(common::ErrCode::kSuccess));
    EXPECT_STREQ("库存不足", common::toMessage(common::ErrCode::kStockNotEnough));
    EXPECT_STREQ("签名校验失败", common::toMessage(common::ErrCode::kSignVerifyFailed));
    EXPECT_TRUE(common::isSuccess(common::ErrCode::kSuccess));
    EXPECT_FALSE(common::isSuccess(common::ErrCode::kStockNotEnough));
}

// ---------------------------------------------------------------------------
// Result：成功与失败两条路径的字段和 JSON 结构。
// 这里断言的是对外协议 —— data 恒存在（失败时为 null），requestId 恒存在。
// ---------------------------------------------------------------------------
TEST(SmokeTest, ResultOkSerializes) {
    const auto result = models::Result<int>::ok(42, "3f2a9c1e-8b74-4d6a-9f01-2c5e7a8b1d3f");

    EXPECT_TRUE(result.isOk());
    EXPECT_EQ(0, result.code);
    ASSERT_TRUE(result.data.has_value());
    EXPECT_EQ(42, result.data.value());

    const Json::Value json = result.toJson();
    EXPECT_EQ(0, json["code"].asInt());
    EXPECT_EQ(42, json["data"].asInt());
    EXPECT_EQ("3f2a9c1e-8b74-4d6a-9f01-2c5e7a8b1d3f", json["requestId"].asString());
    EXPECT_FALSE(json["message"].asString().empty());
}

TEST(SmokeTest, ResultFailSerializesNullData) {
    const auto result =
        models::Result<int>::fail(common::ErrCode::kStockNotEnough, "库存不足", "req-1");

    EXPECT_FALSE(result.isOk());
    EXPECT_EQ(30002, result.code);
    EXPECT_FALSE(result.data.has_value());

    const Json::Value json = result.toJson();
    EXPECT_EQ(30002, json["code"].asInt());
    EXPECT_EQ("库存不足", json["message"].asString());
    // 关键契约：失败时 data 字段必须存在且为 null，而不是整个字段消失
    EXPECT_TRUE(json.isMember("data"));
    EXPECT_TRUE(json["data"].isNull());
}

// 带 toJson() 的领域对象走另一条序列化分支（has_to_json 为 true）
namespace {
struct FakeModel {
    int id = 7;
    Json::Value toJson() const {
        Json::Value v;
        v["id"] = id;
        return v;
    }
};
}  // namespace

TEST(SmokeTest, ResultSerializesModelWithToJson) {
    const auto result = models::Result<FakeModel>::ok(FakeModel{7}, "req-2");

    const Json::Value json = result.toJson();
    EXPECT_EQ(7, json["data"]["id"].asInt());
}

// ---------------------------------------------------------------------------
// BizException：错误码能被上层拿到，what() 能拿到可读原因。
// ---------------------------------------------------------------------------
TEST(SmokeTest, BizExceptionCarriesCode) {
    const common::BizException ex(common::ErrCode::kBalanceNotEnough, "余额不足");

    EXPECT_EQ(20002, static_cast<int>(ex.code()));
    EXPECT_EQ(20002, ex.intCode());
    EXPECT_STREQ("余额不足", ex.what());

    // 必须以 std::exception 的身份被捕获 —— Service 层会统一 catch 它
    const std::exception& base = ex;
    EXPECT_STREQ("余额不足", base.what());
}
