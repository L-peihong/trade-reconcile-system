#!/usr/bin/env bash
# 生成模拟渠道账单
#
# 用法：bash scripts/gen_bill.sh <YYYY-MM-DD> [--missing N] [--extra N] [--tamper N]
#   --missing N : 删掉账单最后 N 行    → 对账产生「本地多单」(类型1)
#   --extra N   : 追加 N 行伪造交易    → 对账产生「渠道多单」(类型2)
#   --tamper N  : 前 N 行金额改为 0.01 → 对账产生「金额不一致」(类型3)
#
# 账单格式（CSV，无表头）：channelTradeNo,orderNo,amount,callbackTime
# 数据来源：t_payment 中 MOCK 渠道、指定日期（按 callback_time 归集）的成功记录，
# 与对账的本地口径一致。
#
# 前置：compose 的 MySQL(3307) 已启动，容器内有 mariadb-client。
# 产物：scripts/bills/bill_<日期>.csv（不提交到 git）
set -euo pipefail

DATE="${1:?用法: bash scripts/gen_bill.sh <YYYY-MM-DD> [--missing N] [--extra N] [--tamper N]}"
MISSING=0
EXTRA=0
TAMPER=0

shift
while [ $# -gt 0 ]; do
    case "$1" in
        --missing) MISSING="${2:?--missing 需要一个数字}"; shift 2 ;;
        --extra)   EXTRA="${2:?--extra 需要一个数字}";   shift 2 ;;
        --tamper)  TAMPER="${2:?--tamper 需要一个数字}";  shift 2 ;;
        *) echo "未知参数: $1" >&2; exit 1 ;;
    esac
done

OUT_DIR="scripts/bills"
OUT="${OUT_DIR}/bill_${DATE}.csv"
mkdir -p "$OUT_DIR"

mysql -h host.docker.internal -P 3307 -uroot -proot123 trade_reconcile -N -B -e \
"SELECT CONCAT_WS(',', channel_trade_no, order_no, CAST(amount AS CHAR), DATE_FORMAT(callback_time, '%Y-%m-%d %H:%i:%s'))
   FROM t_payment
  WHERE channel='MOCK' AND status=1
    AND callback_time >= '${DATE} 00:00:00'
    AND callback_time <  DATE_ADD('${DATE} 00:00:00', INTERVAL 1 DAY)
  ORDER BY id;" > "$OUT"

TOTAL=$(wc -l < "$OUT")

# 差异注入
if [ "$TAMPER" -gt 0 ]; then
    # 前 N 行金额改为 0.01（两侧都在，金额不等 → 类型3）
    head -n "$TAMPER" "$OUT" | awk -F',' '{$3="0.01"; print $1","$2","$3","$4}' OFS=',' > "$OUT.tmp"
    tail -n +$((TAMPER + 1)) "$OUT" >> "$OUT.tmp"
    mv "$OUT.tmp" "$OUT"
fi

if [ "$MISSING" -gt 0 ]; then
    # 删最后 N 行（本地有渠道无 → 类型1）
    head -n -"$MISSING" "$OUT" > "$OUT.tmp"
    mv "$OUT.tmp" "$OUT"
fi

if [ "$EXTRA" -gt 0 ]; then
    # 追加 N 行伪造交易（渠道有本地无 → 类型2）
    TS=$(date +%s)
    for i in $(seq 1 "$EXTRA"); do
        printf 'MOCKEXTRA%s%04d,ORDER_UNKNOWN_%04d,199.00,%s 12:00:00\n' "$TS" "$i" "$i" "$DATE" >> "$OUT"
    done
fi

FINAL=$(wc -l < "$OUT")
echo "账单已生成: $OUT (本地原始 $TOTAL 行,注入后 $FINAL 行)"
