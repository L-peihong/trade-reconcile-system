// 对账差异分类单测：逐笔核对逻辑（纯函数，无 DB 依赖）
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "models/Reconcile.h"
#include "services/ReconcileService.h"

using models::ReconcileSideRow;
using services::ClassifiedDiff;
using services::classifyReconcileDiffs;

namespace {

ReconcileSideRow makeRow(const std::string& tradeNo, const std::string& orderNo,
                         std::int64_t fen) {
    ReconcileSideRow r;
    r.channelTradeNo = tradeNo;
    r.orderNo = orderNo;
    r.amountFen = fen;
    return r;
}

}  // namespace

TEST(ReconcileDiffTest, PerfectMatchProducesNoDiff) {
    std::vector<ReconcileSideRow> local = {makeRow("T1", "O1", 19900)};
    std::vector<ReconcileSideRow> channel = {makeRow("T1", "O1", 19900)};

    const auto diffs = classifyReconcileDiffs(local, channel);
    EXPECT_TRUE(diffs.empty());
}

TEST(ReconcileDiffTest, LocalOnlyRow) {
    std::vector<ReconcileSideRow> local = {
        makeRow("T1", "O1", 19900),
        makeRow("T2", "O2", 599900),  // 渠道账单里没有
    };
    std::vector<ReconcileSideRow> channel = {makeRow("T1", "O1", 19900)};

    const auto diffs = classifyReconcileDiffs(local, channel);
    ASSERT_EQ(diffs.size(), 1u);
    EXPECT_EQ(diffs[0].type, models::ReconcileDiffType::kLocalOnly);
    EXPECT_EQ(diffs[0].channelTradeNo, "T2");
    EXPECT_EQ(diffs[0].orderNo, "O2");
    EXPECT_EQ(diffs[0].localFen, 599900);
    EXPECT_EQ(diffs[0].channelFen, 0);
    EXPECT_EQ(diffs[0].diffFen, 599900);  // 本地 − 渠道(0)
}

TEST(ReconcileDiffTest, ChannelOnlyRow) {
    std::vector<ReconcileSideRow> local = {makeRow("T1", "O1", 19900)};
    std::vector<ReconcileSideRow> channel = {
        makeRow("T1", "O1", 19900),
        makeRow("TX", "OX", 19900),  // 本地没有
    };

    const auto diffs = classifyReconcileDiffs(local, channel);
    ASSERT_EQ(diffs.size(), 1u);
    EXPECT_EQ(diffs[0].type, models::ReconcileDiffType::kChannelOnly);
    EXPECT_EQ(diffs[0].channelTradeNo, "TX");
    EXPECT_EQ(diffs[0].localFen, 0);
    EXPECT_EQ(diffs[0].channelFen, 19900);
    EXPECT_EQ(diffs[0].diffFen, -19900);  // 本地(0) − 渠道
}

TEST(ReconcileDiffTest, AmountMismatch) {
    std::vector<ReconcileSideRow> local = {makeRow("T1", "O1", 19900)};
    std::vector<ReconcileSideRow> channel = {makeRow("T1", "O1", 1)};  // 被篡改成 0.01

    const auto diffs = classifyReconcileDiffs(local, channel);
    ASSERT_EQ(diffs.size(), 1u);
    EXPECT_EQ(diffs[0].type, models::ReconcileDiffType::kAmountMismatch);
    EXPECT_EQ(diffs[0].diffFen, 19899);  // 19900 − 1
}

TEST(ReconcileDiffTest, MixedScenarios) {
    std::vector<ReconcileSideRow> local = {
        makeRow("T1", "O1", 19900),   // 一致
        makeRow("T2", "O2", 19900),   // 本地多单
        makeRow("T3", "O3", 599900),  // 金额不一致
    };
    std::vector<ReconcileSideRow> channel = {
        makeRow("T1", "O1", 19900),
        makeRow("T3", "O3", 1),       // 金额被改
        makeRow("TX", "OX", 19900),   // 渠道多单
    };

    const auto diffs = classifyReconcileDiffs(local, channel);
    ASSERT_EQ(diffs.size(), 3u);

    int localOnly = 0;
    int channelOnly = 0;
    int amountMismatch = 0;
    for (const auto& d : diffs) {
        switch (d.type) {
            case models::ReconcileDiffType::kLocalOnly: ++localOnly; break;
            case models::ReconcileDiffType::kChannelOnly: ++channelOnly; break;
            case models::ReconcileDiffType::kAmountMismatch: ++amountMismatch; break;
        }
    }
    EXPECT_EQ(localOnly, 1);
    EXPECT_EQ(channelOnly, 1);
    EXPECT_EQ(amountMismatch, 1);
}
