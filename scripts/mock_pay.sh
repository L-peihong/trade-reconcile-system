#!/usr/bin/env bash
# 模拟渠道支付回调
#
# 签名协议与 src/utils/SignUtil.h 一致：
#   canonical = orderNo|channelTradeNo|amount|timestamp
#   sign      = hex(HMAC-SHA256(secret, canonical))
#
# 用法：bash scripts/mock_pay.sh <orderNo> [amount]
#   amount 缺省时从库取订单金额（需要 mariadb-client 与 compose 的 MySQL 3307）
# 前置：trade_server 已启动
set -euo pipefail

SECRET="mock-channel-secret-2026"
BASE_URL="${BASE_URL:-http://localhost:8080}"
ORDER_NO="${1:?用法: bash scripts/mock_pay.sh <orderNo> [amount]}"
AMOUNT="${2:-}"

# 未给金额就从库取订单金额（CAST 成 CHAR 保住两位小数格式，与下单一致）
if [ -z "$AMOUNT" ]; then
    AMOUNT=$(mysql -h host.docker.internal -P 3307 -uroot -proot123 trade_reconcile -N -e \
        "SELECT CAST(total_amount AS CHAR) FROM t_order WHERE order_no='${ORDER_NO}';")
    if [ -z "$AMOUNT" ]; then
        echo "订单不存在: ${ORDER_NO}" >&2
        exit 1
    fi
fi

CHANNEL_TRADE_NO="MOCK$(date +%s)"
TS=$(date +%s)
CANONICAL="${ORDER_NO}|${CHANNEL_TRADE_NO}|${AMOUNT}|${TS}"
SIGN=$(printf '%s' "$CANONICAL" | openssl dgst -sha256 -hmac "$SECRET" -hex | awk '{print $2}')
REQ_ID=$(printf '%08x%08x' "$TS" "$$")

echo "==> 模拟渠道回调: order=${ORDER_NO} amount=${AMOUNT} trade=${CHANNEL_TRADE_NO}"
curl -s -X POST "${BASE_URL}/api/v1/payment/callback" \
    -H 'Content-Type: application/json' \
    -H "X-Request-Id: ${REQ_ID}" \
    -d "{\"orderNo\":\"${ORDER_NO}\",\"channel\":\"MOCK\",\"channelTradeNo\":\"${CHANNEL_TRADE_NO}\",\"amount\":\"${AMOUNT}\",\"timestamp\":${TS},\"sign\":\"${SIGN}\"}"
echo
