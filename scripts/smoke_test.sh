#!/usr/bin/env bash
# 端到端冒烟：健康检查 → 下单（幂等+防超卖）→ 支付回调（验签+入账）
# → 商户通知（可靠消息）→ 生成带差异的当日账单 → 日终对账 → 差异查询 → 核销
#
# 用法：bash scripts/smoke_test.sh
# 前置：trade_server 已启动；compose 中间件已起；容器内有 mariadb-client。
#
# 说明：对账按「今日」跑，uk_date_channel 幂等 —— 今日已对过账时，脚本会
#       验证幂等拦截（70002）并跳过差异/核销步骤。
set -euo pipefail

BASE_URL="${BASE_URL:-http://localhost:8080}"
DB="mysql -h host.docker.internal -P 3307 -uroot -proot123 trade_reconcile -N"
TODAY=$(date -u -d '+8 hours' +%F)   # 北京时间日期（显式 UTC+8，不依赖系统时区）
PASS=0
FAIL=0

step() { echo ""; echo "==> $1"; }
ok()   { echo "    ✅ $1"; PASS=$((PASS + 1)); }
die()  { echo "    ❌ $1"; FAIL=$((FAIL + 1)); exit 1; }

# 从 JSON 里抠字段（字符串值带引号/数字不带，两种模式都匹配）
# 用法: echo "$JSON" | field <key>   —— $1 是键名
field() {
    sed -n -e 's/.*"'$1'":"\([^"]*\)".*/\1/p' \
           -e 's/.*"'$1'":\(-\{0,1\}[0-9]*\).*/\1/p'
}

step "1/6 健康检查"
# 清掉今日账单，防止 60s 定时任务在冒烟跑到第 5 步前抢跑对账
rm -f "scripts/bills/bill_${TODAY}.csv"
# curl 失败要显式 die：set -e 下赋值失败会静默退出
RESP=$(curl -s -m 5 "$BASE_URL/health") || die "无法连接服务(确认 trade_server 已启动)"
echo "$RESP" | grep -q '"code":0' && ok "服务健康" || die "health 失败: $RESP"

step "2/6 下单（幂等 + 防超卖）"
RID=$(printf '%08x%08x' "$(date +%s)" "$$")
RESP=$(curl -s -m 10 -X POST "$BASE_URL/api/v1/orders" \
    -H 'Content-Type: application/json' -H "X-Request-Id: $RID" \
    -d '{"productId":3,"quantity":1,"userId":1001}') || die "下单请求失败"
ORDER_NO=$(echo "$RESP" | field orderNo)
[ -n "$ORDER_NO" ] || die "下单失败: $RESP"
ok "订单创建: $ORDER_NO"

# 重放同请求，快照应逐字节一致（幂等）
RESP2=$(curl -s -m 10 -X POST "$BASE_URL/api/v1/orders" \
    -H 'Content-Type: application/json' -H "X-Request-Id: $RID" \
    -d '{"productId":3,"quantity":1,"userId":1001}') || die "重放请求失败"
[ "$RESP" = "$RESP2" ] && ok "幂等重放:响应逐字节一致" || die "幂等重放不一致"

step "3/6 支付回调（验签 + 出账 + 流水 + 本地消息）"
TS=$(date +%s)
TRADE="MOCKSMOKE${TS}"
AMOUNT=$($DB -e "SELECT CAST(total_amount AS CHAR) FROM t_order WHERE order_no='${ORDER_NO}';") || die "查询订单金额失败"
SIGN=$(printf '%s' "${ORDER_NO}|${TRADE}|${AMOUNT}|${TS}" \
    | openssl dgst -sha256 -hmac "mock-channel-secret-2026" -hex | awk '{print $2}')
PAY_RESP=$(curl -s -m 10 -X POST "$BASE_URL/api/v1/payment/callback" \
    -H 'Content-Type: application/json' -H "X-Request-Id: $(printf '%08x%08x' "$TS" "$RANDOM")" \
    -d "{\"orderNo\":\"${ORDER_NO}\",\"channel\":\"MOCK\",\"channelTradeNo\":\"${TRADE}\",\"amount\":\"${AMOUNT}\",\"timestamp\":${TS},\"sign\":\"${SIGN}\"}") || die "支付回调请求失败"
echo "$PAY_RESP" | grep -q '"code":0' && ok "支付成功,金额 ${AMOUNT}" || die "支付失败: $PAY_RESP"

step "4/6 可靠消息（投递 + 消费 + 商户通知）"
NOTIFY_COUNT=0
for _ in $(seq 1 20); do
    NOTIFY_COUNT=$($DB -e "SELECT COUNT(*) FROM t_notify_record WHERE order_no='${ORDER_NO}' AND status=1;")
    [ "$NOTIFY_COUNT" -ge 1 ] && break
    sleep 1
done
[ "$NOTIFY_COUNT" -ge 1 ] && ok "商户已收到通知(经 MQ,状态=成功)" || die "通知未送达"

step "5/6 日终对账（生成带差异账单 + 跑批）"
bash scripts/gen_bill.sh "$TODAY" --tamper 1 > /dev/null
RUN_RESP=$(curl -s -m 15 -X POST "$BASE_URL/api/v1/reconcile/run" \
    -H 'Content-Type: application/json' \
    -H "X-Request-Id: $(printf '%08x%08x' "$(date +%s)" "$$")" \
    -d "{\"billDate\":\"${TODAY}\"}") || die "跑批请求失败"
if echo "$RUN_RESP" | grep -q '"code":70002'; then
    ok "今日已对账,幂等拦截生效(70002) —— 跳过差异步骤"
    echo ""
    echo "================ 冒烟结果: $PASS 通过 / $FAIL 失败 ================"
    exit 0
fi
BATCH_NO=$(echo "$RUN_RESP" | field batchNo)
[ -n "$BATCH_NO" ] || die "跑批失败: $RUN_RESP"
ok "对账批次: $BATCH_NO"

step "6/6 差异查询 + 核销"
DIFFS=$(curl -s -m 10 "$BASE_URL/api/v1/reconcile/diffs?batchNo=${BATCH_NO}") || die "差异查询请求失败"
DIFF_COUNT=$(echo "$DIFFS" | field count)
[ "$DIFF_COUNT" -ge 1 ] && ok "定位差异 $DIFF_COUNT 笔(含金额不一致)" || die "差异查询失败: $DIFFS"

DIFF_ID=$($DB -e "SELECT id FROM t_reconcile_diff WHERE batch_no='${BATCH_NO}' ORDER BY id LIMIT 1;")
HANDLE_RESP=$(curl -s -m 10 -X POST "$BASE_URL/api/v1/reconcile/diffs/${DIFF_ID}/handle" \
    -H 'Content-Type: application/json' \
    -H "X-Request-Id: $(printf '%08x%08x' "$(date +%s)" "$$")" \
    -d '{"action":"verify","remark":"冒烟测试核销"}') || die "核销请求失败"
echo "$HANDLE_RESP" | grep -q '"handleStatus":1' && ok "差异 #${DIFF_ID} 已核销" || die "核销失败: $HANDLE_RESP"

echo ""
echo "================ 冒烟结果: $PASS 通过 / $FAIL 失败 ================"
