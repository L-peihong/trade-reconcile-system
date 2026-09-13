#include "dao/PaymentDao.h"

#include <cstdint>

#include "utils/MoneyUtil.h"

namespace dao {

PaymentInsertOutcome PaymentDao::insertWithDedup(
    const std::shared_ptr<drogon::orm::Transaction>& tx,
    const models::Payment& payment) {

    // uk_channel_trade(channel, channel_trade_no) 兜底渠道重复回调；
    // callback_time 用渠道时间戳，应用侧转成北京时间字符串后入参（对账按此归集）。
    auto result = tx->execSqlSync(
        "INSERT INTO t_payment"
        " (payment_no, order_no, channel, channel_trade_no,"
        "  amount, status, callback_time, raw_body)"
        " VALUES (?, ?, ?, ?,"
        "  CAST(? AS DECIMAL(18,2)), 1, ?, ?)"
        " ON DUPLICATE KEY UPDATE payment_no = payment_no",
        payment.paymentNo,
        payment.orderNo,
        payment.channel,
        payment.channelTradeNo,
        utils::fenToYuanString(payment.amountFen),
        payment.callbackTime,
        payment.rawBody);

    if (result.affectedRows() == 1) {
        return PaymentInsertOutcome::kInserted;
    }
    return PaymentInsertOutcome::kDuplicate;
}

std::optional<models::Payment> PaymentDao::getByChannelTradeNo(
    const std::shared_ptr<drogon::orm::Transaction>& tx,
    const std::string& channel,
    const std::string& channelTradeNo) {

    auto result = tx->execSqlSync(
        "SELECT payment_no, order_no, channel, channel_trade_no,"
        "       CAST(amount * 100 AS SIGNED) AS amount_fen, status"
        "  FROM t_payment"
        " WHERE channel = ? AND channel_trade_no = ?",
        channel, channelTradeNo);

    if (result.empty()) {
        return std::nullopt;
    }

    const auto& row = result[0];
    models::Payment p;
    p.paymentNo      = row["payment_no"].as<std::string>();
    p.orderNo        = row["order_no"].as<std::string>();
    p.channel        = row["channel"].as<std::string>();
    p.channelTradeNo = row["channel_trade_no"].as<std::string>();
    p.amountFen      = row["amount_fen"].as<long long>();
    p.status         = static_cast<models::Payment::Status>(row["status"].as<int>());
    return p;
}

}  // namespace dao
