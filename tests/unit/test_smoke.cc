// 冒烟测试：验证构建链路本身是通的
// 能编译、能链接 trade_core、能跑起来、ctest 能发现它。
// 构建出问题时先跑这个，区分是构建坏了还是业务写错了。
#include <gtest/gtest.h>

#include <string>

#include "common/BizException.h"
#include "common/ErrorCode.h"
#include "models/Result.h"

// 这个用例本身验不了 trade_core 的链接：静态库只有符号被引用才会被链接器
// 拉进来，没人引用等于没链。下面几个调用业务代码的用例才是真正的自检。
TEST(SmokeTest, Trivial) {
    EXPECT_EQ(1, 1);
}

// 错误码数值是对外协议契约，改了会破坏兼容；顺带验文案
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

// Result 两条路径的 JSON 结构：data 恒存在（失败为 null），requestId 恒存在
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

// BizException：错误码能被上层拿到，what() 给出可读原因
TEST(SmokeTest, BizExceptionCarriesCode) {
    const common::BizException ex(common::ErrCode::kBalanceNotEnough, "余额不足");

    EXPECT_EQ(20002, static_cast<int>(ex.code()));
    EXPECT_EQ(20002, ex.intCode());
    EXPECT_STREQ("余额不足", ex.what());

    // 必须以 std::exception 的身份被捕获 —— Service 层会统一 catch 它
    const std::exception& base = ex;
    EXPECT_STREQ("余额不足", base.what());
}
