# 接口清单

Base URL: `http://localhost:8080`
错误码定义见 `src/common/ErrorCode.h`。
写接口(POST/PUT/DELETE)必须带 `X-Request-Id`(UUID v4 或 32 位十六进制,16~64 位),缺失返回 `400/10002`。

## 健康检查

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/health` | 服务健康,`{"code":0,"data":{"status":"up"}}` |

## 交易链路

### 创建订单

```
POST /api/v1/orders
Headers: X-Request-Id(必填)
Body:   {"productId": 1, "quantity": 2, "userId": 1001}
```

- 成功 `code=0`:`data` 含 orderNo(雪花)/unitPrice/totalAmount/expireTime(北京时间)
- 同一 X-Request-Id 重放 → 返回与首次**逐字节一致**的快照
- 库存不足 → `code=30002`;商品不存在/下架 → `code=30001`

### 渠道支付回调(MOCK)

```
POST /api/v1/payment/callback
Headers: X-Request-Id(必填)
Body:   {"orderNo":"...","channel":"MOCK","channelTradeNo":"...",
         "amount":"5999.00","timestamp":1789275496,"sign":"<hex>"}
```

签名协议:`sign = hex(HMAC-SHA256(secret, orderNo|channelTradeNo|amount|timestamp))`,secret 在 config 的 `custom_config.mock_channel`。生成签名直接用 `scripts/mock_pay.sh <orderNo>`。

- 成功 `code=0`:`data.status=1`(订单已支付,账户出账,流水/本地消息落库)
- 同 channelTradeNo 重放 → 幂等返回成功,不重复入账
- 验签失败 → `50001`;金额与订单不符 → `50002`;交易号绑其他订单 → `50003`

## 管理接口(对账)

### 触发对账

```
POST /api/v1/reconcile/run
Body: {"billDate":"2026-09-13"}
```

账单文件须存在:`scripts/bills/bill_<日期>.csv`(用 `scripts/gen_bill.sh <日期> [--missing N] [--extra N] [--tamper N]` 生成)。
成功返回批次号与差异笔数;同日重复跑批 → `70002`。

### 差异查询

```
GET /api/v1/reconcile/diffs?batchNo=<批次号>
```

返回差异明细:diffType(1 本地多单 / 2 渠道多单 / 3 金额不一致)、两侧金额、差额。

### 差异核销/忽略

```
POST /api/v1/reconcile/diffs/{id}/handle
Body: {"action":"verify|ignore","remark":"与渠道确认"}
```

重复处理 → `70003`。

## 模拟端点

### 商户通知落点

```
POST /api/v1/mock/notify        (MQ 消费者的通知目标)
Body: {"orderNo":"...","amount":"199.00","fail":false}
```

`fail:true` 模拟商户故障(返回 500),用于演示消费重试与死信。
