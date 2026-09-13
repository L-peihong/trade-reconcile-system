#!/usr/bin/env bash
# ============================================================================
# 防超卖并发压测（亮点①验收）—— 100 并发抢 50 件库存（P1002）
#
# 前置条件：
#   1. trade_server 已启动（容器内 ./build/debug/trade_server）
#   2. 容器内已装 mariadb-client（apt-get install -y mariadb-client）
#   3. 宿主机 compose 的 MySQL 已映射 3307（CLAUDE.md 6.7）
#
# 用法：bash scripts/stress_stock.sh
# 产出验收数据：成功订单数恰为 50、剩余库存恰为 0、零负库存（零超卖）。
#
# 注意：脚本会重置 P1002 库存为 50，并清空商品 2 的历史压测订单与全部
# 下单幂等记录（开发库专用，不影响其他数据）。
# ============================================================================
set -euo pipefail

BASE_URL="${BASE_URL:-http://localhost:8080}"
DB="mysql -h host.docker.internal -P 3307 -uroot -proot123 trade_reconcile -N"
PRODUCT_ID=2
STOCK=50
CONCURRENCY=100

echo "==> 重置压测前置状态:商品 $PRODUCT_ID 库存 = $STOCK"
$DB -e "UPDATE t_product SET stock=$STOCK, version=0 WHERE id=$PRODUCT_ID;"
$DB -e "DELETE FROM t_order WHERE product_id=$PRODUCT_ID;"
$DB -e "DELETE FROM t_idempotent WHERE api_path='/api/v1/orders';"

TS=$(date +%s)
OUT_DIR=$(mktemp -d)
trap 'rm -rf "$OUT_DIR"' EXIT

echo "==> 启动 $CONCURRENCY 个并发下单请求(每个 quantity=1)..."
for i in $(seq 1 "$CONCURRENCY"); do
    # 16 位十六进制 request_id：满足 X-Request-Id 校验规则([0-9a-fA-F-], 16~64 位)
    REQ_ID=$(printf '%08x%08x' "$TS" "$i")
    (
        curl -s -X POST "$BASE_URL/api/v1/orders" \
            -H 'Content-Type: application/json' \
            -H "X-Request-Id: $REQ_ID" \
            -d "{\"productId\":$PRODUCT_ID,\"quantity\":1,\"userId\":1001}" \
            -o "$OUT_DIR/$i.json" -w '%{http_code}' > "$OUT_DIR/$i.http"
    ) &
done
wait

# 统计：成功(code=0)与库存不足拒绝(30002)。
# grep 同时匹配紧凑格式 "code":0 与美化格式 "code" : 0。
SUCCESS=$(grep -lE '"code" ?: ?0' "$OUT_DIR"/*.json 2>/dev/null | wc -l)
SOLD_OUT=$(grep -lE '30002' "$OUT_DIR"/*.json 2>/dev/null | wc -l)

REMAIN=$($DB -e "SELECT stock FROM t_product WHERE id=$PRODUCT_ID;")
ORDERS=$($DB -e "SELECT COUNT(*) FROM t_order WHERE product_id=$PRODUCT_ID;")
NEG_CHECK=$($DB -e "SELECT COUNT(*) FROM t_product WHERE id=$PRODUCT_ID AND stock < 0;")

echo ""
echo "================ 压测结果（防超卖验收） ================"
echo "并发请求数   : $CONCURRENCY"
echo "成功下单数   : $SUCCESS      （期望: $STOCK）"
echo "库存不足拒绝 : $SOLD_OUT      （期望: $((CONCURRENCY - STOCK))）"
echo "剩余库存     : $REMAIN        （期望: 0）"
echo "订单落库数   : $ORDERS        （期望: $STOCK）"
echo "负库存记录数 : $NEG_CHECK      （期望: 0 —— 零超卖）"
echo "====================================================="

if [ "$SUCCESS" -eq "$STOCK" ] && [ "$REMAIN" -eq 0 ] &&
   [ "$ORDERS" -eq "$STOCK" ] && [ "$NEG_CHECK" -eq 0 ]; then
    echo "验收通过 ✅  —— 这组数字可以写进简历"
    exit 0
else
    echo "验收失败 ❌ —— 检查服务端日志 logs/trade_server.log"
    exit 1
fi
