# CLAUDE.md

本文件为 Claude Code 及协作者提供本仓库的上下文约束。**动手前先读完第 4、6 节**，那里是踩过坑之后沉淀下来的硬性约定。

---

## 1. 项目简介与技术栈

**高可靠交易与对账系统** —— 一个面向 C++ 后端岗位的作品级 MVP，用 7 天完成。核心不是业务广度，而是把「钱不能算错、消息不能丢、重复请求不能重复扣款」这三件事做扎实。

围绕一笔交易的生命周期串联七条主线：

1. **下单** —— 幂等校验 → 校验商品与库存 → 扣减库存（防超卖）→ 创建订单
2. **支付回调** —— 渠道异步回调 → 验签 → 幂等落库 → 更新订单状态 → 账户入账
3. **可靠消息** —— 业务数据与本地消息表**同事务**写入 → 异步投递 RabbitMQ → 下游消费
4. **异步通知** —— 消费者驱动通知任务，指数退避重试至成功或进死信
5. **日终对账** —— 拉取渠道账单 → 与本地流水逐笔核对 → 生成差异明细 → 管理接口人工处理
6. **管理接口** —— 差异查询、差异核销、批次重跑、库存/账户查询
7. **可观测性** —— 全链路 `request_id` 贯穿 HTTP / MQ / 日志

### 技术栈

| 层次 | 选型 | 版本 | 用途 |
|---|---|---|---|
| 语言 | C++ | C++17 | `std::optional` / `shared_mutex` / 结构化绑定 |
| Web 框架 | Drogon | 1.9.x | HTTP 服务、ORM、异步 DbClient、定时任务 |
| 数据库 | MySQL | 8.0 | 事务与行锁的载体，InnoDB + `REPEATABLE READ` |
| 缓存 | Redis | 7 | 幂等 token、分布式锁、热点商品缓存 |
| 消息队列 | RabbitMQ | 3.12+ | 异步解耦，手动 ACK + 死信队列 |
| 容器 | Docker Compose | v2 | 一键拉起全部中间件 |
| 单元测试 | GoogleTest | 1.14 | Service 层与工具类测试 |
| 日志 | spdlog | 1.13 | 异步 logger，结构化输出 |
| 构建 | CMake | 3.20+ | 统一 dev container 与 CI 的构建入口 |

### 开发环境

Windows 11 宿主机 + Docker Desktop + VSCode Dev Containers，容器内为 **Ubuntu 22.04**。
**所有编译、测试、运行命令都在容器内执行**，宿主机只负责跑 `docker compose` 和编辑代码。

---

## 2. 目录结构

```
trade-reconcile-system/
├── CLAUDE.md                  # 本文件
├── CMakeLists.txt             # 顶层构建入口
├── CMakePresets.json          # debug / release / test 三套预设
├── docker-compose.yml         # mysql + redis + rabbitmq + app
├── Dockerfile                 # 多阶段构建：builder → runtime
├── .devcontainer/             # Dev Containers 配置
├── .gitattributes             # 强制 LF，见第 6 节
├── config/
│   └── config.json            # Drogon 主配置（DB / Redis / 端口 / 日志等级）
├── sql/
│   ├── schema.sql             # 建表 DDL，容器首次启动自动执行
│   └── seed.sql               # 测试商品与账户初始数据
├── scripts/
│   ├── smoke_test.sh          # 端到端冒烟：下单 → 回调 → 对账
│   └── gen_bill.sh            # 生成模拟渠道账单，用于对账演示
├── docs/
│   ├── api.md                 # 接口清单
│   ├── error_code.md          # 错误码全表
│   └── design.md              # 关键设计决策与取舍（面试讲这里）
├── src/
│   ├── main.cc                # 入口：加载配置 → 注册 controllers → 启动定时任务
│   ├── controllers/           # HTTP 层：参数解析、鉴权、调用 Service、组装响应
│   │   ├── AccountController
│   │   ├── ProductController
│   │   ├── OrderController
│   │   ├── PaymentCallbackController
│   │   └── AdminController
│   ├── services/              # 业务层：事务边界、编排、领域校验
│   │   ├── AccountService
│   │   ├── ProductService
│   │   ├── OrderService
│   │   ├── PaymentService
│   │   ├── IdempotentService
│   │   ├── LocalMessageService
│   │   └── ReconcileService
│   ├── dao/                   # 数据访问层：单表 CRUD，不开事务
│   │   ├── AccountDao
│   │   ├── ProductDao
│   │   ├── OrderDao
│   │   ├── PaymentDao
│   │   ├── IdempotentDao
│   │   ├── LocalMessageDao
│   │   └── ReconcileDao
│   ├── models/                # 领域实体与 DTO，纯数据结构，无业务逻辑
│   │   ├── Account.h
│   │   ├── Product.h
│   │   ├── Order.h
│   │   ├── Payment.h
│   │   ├── LocalMessage.h
│   │   ├── Reconcile.h
│   │   └── Result.h           # 统一返回体 Result<T>
│   ├── tasks/                 # 定时任务：由 Drogon 的定时器驱动
│   │   ├── MessageRelayTask    # 扫描本地消息表 → 投递 MQ
│   │   ├── NotifyRetryTask     # 通知失败重试
│   │   ├── ReconcileTask       # 日终对账
│   │   └── OrderTimeoutTask    # 超时未支付订单关单
│   ├── consumer/              # MQ 消费者：手动 ACK + 幂等
│   │   └── NotifyConsumer
│   ├── common/                # 横切关注点
│   │   ├── ErrorCode.h        # 错误码枚举，见第 4 节
│   │   ├── BizException.h     # 业务异常，携带错误码
│   │   ├── RequestIdFilter    # 注入/校验 X-Request-Id
│   │   ├── Logger.h           # spdlog 封装，绑 request_id
│   │   └── Constants.h
│   └── utils/
│       ├── IdGenerator        # 雪花算法生成订单号
│       ├── SignUtil           # 回调验签
│       ├── MoneyUtil          # 分为单位的整数运算与展示转换
│       ├── DbUtil             # 事务 RAII 封装、受影响行数校验
│       └── JsonUtil
└── tests/
    ├── CMakeLists.txt
    ├── unit/                  # 纯逻辑单测：金额计算、幂等 key、状态机
    ├── integration/           # 连真实 MySQL/Redis 的集成测试
    └── data/                  # 测试夹具
```

### 分层职责（**不要越界**）

| 层 | 可以做 | 不可以做 |
|---|---|---|
| Controller | 解析参数、校验 `X-Request-Id`、调 Service、包装响应 | 写 SQL、开事务、写业务分支 |
| Service | **开启/提交事务**、编排多个 DAO、领域校验、抛 BizException | 直接拼 SQL、直接发 MQ |
| DAO | 单表 CRUD、接收外部传入的事务对象 | 开事务、跨表 join 写操作、抛 HTTP 概念 |
| Task/Consumer | 触发 Service 的某个用例 | 绕过 Service 直接改表 |

---

## 3. 构建与运行命令

### 3.1 启动中间件（宿主机执行）

```bash
# 只拉起依赖，本地跑 app 时用；-d 后台
docker compose up -d mysql redis rabbitmq

# 全量拉起（含 app 容器）
docker compose up -d --build

# 看日志 / 进 MySQL
docker compose logs -f app
docker compose exec mysql mysql -uroot -proot123 trade_reconcile
```

首次启动 MySQL 会自动执行 `sql/schema.sql` 与 `sql/seed.sql`（挂载在 `/docker-entrypoint-initdb.d`）。
**改了 DDL 要 `docker compose down -v` 清卷重建**，否则初始化脚本不会重跑。

### 3.2 编译（容器内执行）

```bash
cmake --preset debug                   # 配置，产物在 build/debug
cmake --build build/debug -j$(nproc)   # 编译

# 只编某个 target，加快迭代
cmake --build build/debug --target trade_server -j$(nproc)
```

### 3.3 运行

```bash
./build/debug/trade_server                    # 默认读 config/config.json
./build/debug/trade_server --config ./config/config.json
```

### 3.4 测试

```bash
ctest --test-dir build/debug --output-on-failure        # 全部
ctest --test-dir build/debug -R OrderServiceTest        # 按名字过滤
./build/debug/tests/unit_tests --gtest_filter='*Idempotent*'
```

集成测试依赖真实 MySQL，跑之前先 `docker compose up -d mysql`；连不上会跳过而非失败（通过 `GTEST_SKIP()`）。

### 3.5 常用脚本

```bash
bash scripts/smoke_test.sh      # 端到端跑一遍：下单 → 支付回调 → 触发对账 → 查差异
bash scripts/gen_bill.sh 2026-09-11   # 生成指定日期的模拟渠道账单
```

---

## 4. 编码约定

### 4.1 事务边界在 Service 层

**这是本项目最重要的一条约定。**

- 事务只在 Service 层开启，DAO 永不自行 `newTransaction`。
- 事务对象由 Service 创建后**显式传给** DAO 方法，DAO 签名形如 `updateStock(TransactionPtr tx, ...)`。
- 一个事务内只允许操作 MySQL。**禁止在事务里发 MQ、调 HTTP、调 Redis 之外的任何外部 IO** —— 需要跨系统一致性时走本地消息表（见 4.6 与第 6 节）。
- 事务块内**不写日志到磁盘以外的重活**，不做耗时计算，尽量缩短持锁时间。
- 事务提交失败必须向上抛 `BizException`，不要吞掉异常返回一个"看起来成功"的 Result。
- 用 RAII 封装事务，保证任何路径退出都会回滚或提交，避免手写 `try/catch` 漏掉分支。

```cpp
// Service 层标准写法
Result<OrderVO> OrderService::createOrder(const CreateOrderReq& req) {
    auto tx = DbUtil::beginTransaction();          // RAII，析构未提交则回滚
    try {
        auto idem = idempotentDao_->tryAcquire(tx, req.requestId, ...);
        if (!idem.acquired) return Result<OrderVO>::from(idem.cachedResponse);

        productDao_->deductStock(tx, req.productId, req.quantity);  // 防超卖 SQL，见第 6 节
        auto order = orderDao_->insert(tx, buildOrder(req));
        localMessageDao_->insert(tx, buildPayMessage(order));       // 同事务写本地消息表

        tx->commit();
        return Result<OrderVO>::ok(order);
    } catch (const BizException& e) {
        return Result<OrderVO>::fail(e.code(), e.what());
    }
}
```

### 4.2 写接口必须带 `X-Request-Id`

所有 **POST / PUT / DELETE** 请求必须携带请求头 `X-Request-Id`：

- 格式：UUID v4 或 32 位十六进制，长度 16–64，缺失或格式非法一律返回 `400` + 错误码 `10002`。
- 由 `RequestIdFilter` 统一拦截校验，Controller 内不重复实现。
- 校验通过后写入请求上下文，**贯穿整条链路**：HTTP 日志 → Service 日志 → 本地消息表 `request_id` 字段 → MQ 消息头 → 消费者日志。
- 幂等表以 `request_id` 建唯一索引，这是幂等的最后一道防线。
- GET 接口不强制，但若带了也要透传到日志。

```bash
curl -X POST http://localhost:8080/api/v1/orders \
  -H 'Content-Type: application/json' \
  -H 'X-Request-Id: 3f2a9c1e-8b74-4d6a-9f01-2c5e7a8b1d3f' \
  -d '{"productId":1,"quantity":2}'
```

### 4.3 错误码规范

统一 5 位数字，**`AABBB` 结构**：前 2 位为模块，后 3 位为模块内序号。`0` 表示成功。

| 段 | 模块 | 示例 |
|---|---|---|
| `10xxx` | 通用 / 参数 | `10001` 参数校验失败 · `10002` 缺少 X-Request-Id · `10003` 重复请求（幂等命中） · `10004` 资源不存在 |
| `20xxx` | 账户 | `20001` 账户不存在 · `20002` 余额不足 · `20003` 账户已冻结 · `20004` 余额扣减冲突（乐观锁重试耗尽） |
| `30xxx` | 商品 / 库存 | `30001` 商品不存在 · `30002` 库存不足 · `30003` 商品已下架 · `30004` 扣减库存失败 |
| `40xxx` | 订单 | `40001` 订单不存在 · `40002` 订单状态不允许此操作 · `40003` 订单已支付 · `40004` 订单已超时关闭 |
| `50xxx` | 支付 | `50001` 验签失败 · `50002` 回调金额与订单金额不一致 · `50003` 渠道交易号冲突 · `50004` 不支持的渠道 |
| `60xxx` | 消息 / MQ | `60001` 消息投递失败 · `60002` 消息体序列化失败 · `60003` 消费超过最大重试次数 |
| `70xxx` | 对账 | `70001` 批次不存在 · `70002` 批次状态不允许重跑 · `70003` 差异已处理 · `70004` 账单文件解析失败 |
| `90xxx` | 系统 | `90001` 数据库错误 · `90002` Redis 不可用 · `90003` 内部未知异常 |

约定：

- 错误码常量统一定义在 `src/common/ErrorCode.h`，**禁止在业务代码里写裸数字**。
- `1xxxx`/`9xxxx` 由框架层与兜底异常处理器返回；`2xxxx`–`7xxxx` 由 Service 层通过 `BizException` 抛出。
- 响应体固定为：`{"code":30002,"message":"库存不足","data":null,"requestId":"..."}`。
- `message` 面向调用方可读；**技术细节写日志，不要塞进 `message`**，避免泄露表名与 SQL。

### 4.4 日志用 spdlog

- **禁止** `std::cout` / `printf` / `fprintf`。统一使用 `src/common/Logger.h` 暴露的接口。
- 使用**异步 logger**（`spdlog::async_logger`）并设置队列满时的溢出策略，避免阻塞业务线程。
- 每条业务日志**必须带 `request_id`**。通过 `Logger::withRequestId(rid)` 获取绑定了上下文的 logger，不要手工拼字符串：

```cpp
auto log = Logger::withRequestId(requestId);
log->info("stock deducted, product_id={} qty={} remain={}", pid, qty, remain);
```

- 用**结构化占位符** `{}`，不要字符串拼接 `+`，性能和可读性都更好。
- 级别约定：
  - `trace` —— 逐条 SQL 与出入参，仅 debug 构建开启
  - `debug` —— 关键分支与状态流转
  - `info` —— 业务里程碑：下单成功、回调落库、对账完成
  - `warn` —— 可自愈的异常：幂等命中、重试、库存扣减冲突
  - `error` —— 需要人介入：事务失败、消息投递超限、对账差异
- **禁止**在日志中打印完整手机号、身份证、银行卡号；敏感字段需脱敏后再输出。

### 4.5 命名与代码风格

- 文件名与类名 `PascalCase`，方法名 `camelCase`，成员变量 `camelCase_`（尾下划线），常量 `kPascalCase`。
- 头文件用 `#pragma once`；`include` 顺序：本文件对应头 → C++ 标准库 → 第三方 → 本项目，组间空行。
- 统一用 `std::optional` 表达「可能没有」，不要用 `-1` 或空字符串当哨兵值。
- 统一用 `Result<T>` 表达「成功或业务失败」，异常只用于真正的意外情况。
- 智能指针：默认 `std::unique_ptr`，跨异步回调共享时才用 `std::shared_ptr`；**注意回调捕获 `shared_ptr` 导致的循环引用**（见第 6 节）。
- 所有 `switch` 覆盖枚举必须写 `default` 分支并 `assert(false)` 或抛异常，防止新增枚举值被静默忽略。

### 4.6 异步与消息约定

- 业务数据变更与本地消息表记录**必须同事务写入**。
- MQ 生产端开启 **publisher confirm**；只有收到 ack 才把消息状态置为「已发送」。
- 消费端 `autoAck = false`，**处理成功才 `basicAck`**，失败 `basicNack(requeue = false)` 转死信队列。
- **所有消费者必须幂等**：以 `message_id` 或业务唯一键去重，重复消费直接 ack 丢弃。
- 定时任务（`src/tasks/`）必须有**防重入**：多实例部署时用 Redis 分布式锁，单实例时用原子标志位。

---

## 5. 数据库表清单与关键字段

库名 `trade_reconcile`，字符集 `utf8mb4`，排序规则 `utf8mb4_0900_ai_ci`，引擎 InnoDB。
**金额统一用 `DECIMAL(18,2)` 或 BIGINT 存「分」，绝对不用 `FLOAT` / `DOUBLE`。**
除明确说明外，每张表都有 `id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY`、`created_at`、`updated_at`。

### 5.1 `t_account` —— 账户表

| 字段 | 类型 | 说明 |
|---|---|---|
| `account_no` | VARCHAR(32) | 账户号，**唯一索引** |
| `user_id` | BIGINT UNSIGNED | 用户 ID，唯一索引 |
| `balance` | DECIMAL(18,2) | 可用余额 |
| `frozen_amount` | DECIMAL(18,2) | 冻结金额 |
| `version` | INT UNSIGNED | 乐观锁版本号 |
| `status` | TINYINT | 0 正常 1 冻结 2 注销 |

索引：`uk_account_no`、`uk_user_id`。
约定：`balance` 与 `frozen_amount` 只用于展示与对账，**扣减一律走条件 UPDATE**，不要读出来改完再写回。

### 5.2 `t_account_flow` —— 账户流水表

| 字段 | 类型 | 说明 |
|---|---|---|
| `account_no` | VARCHAR(32) | 账户号 |
| `biz_type` | TINYINT | 1 充值 2 支付 3 退款 4 冻结 5 解冻 |
| `direction` | TINYINT | 1 入账 2 出账 |
| `amount` | DECIMAL(18,2) | 变动金额，恒为正数，方向由 `direction` 表达 |
| `before_balance` / `after_balance` | DECIMAL(18,2) | 变动前后余额，用于对账核验 |
| `order_no` | VARCHAR(32) | 关联订单号，可空 |
| `request_id` | VARCHAR(64) | 来源请求 ID，**唯一索引**（与 `biz_type` 组成联合唯一，防重复记账） |

索引：`uk_request_biz(request_id, biz_type)`、`idx_account_time(account_no, created_at)`。
**流水表只增不改**，是日终对账的本地数据源。

### 5.3 `t_product` —— 商品表

| 字段 | 类型 | 说明 |
|---|---|---|
| `product_no` | VARCHAR(32) | 商品编码，**唯一索引** |
| `name` | VARCHAR(128) | 商品名称 |
| `price` | DECIMAL(18,2) | 单价 |
| `stock` | INT UNSIGNED | 可售库存 |
| `locked_stock` | INT UNSIGNED | 下单锁定库存 |
| `version` | INT UNSIGNED | 乐观锁版本号（辅助手段，主防超卖靠条件更新） |
| `status` | TINYINT | 0 下架 1 上架 |

索引：`uk_product_no`、`idx_status(status)`。

### 5.4 `t_order` —— 订单表

> 表名用 `t_order` 而非 `order` —— `ORDER` 是 MySQL 保留字，见第 6 节。

| 字段 | 类型 | 说明 |
|---|---|---|
| `order_no` | VARCHAR(32) | 业务订单号（雪花），**唯一索引** |
| `request_id` | VARCHAR(64) | 创建请求 ID，**唯一索引**（幂等兜底） |
| `user_id` | BIGINT UNSIGNED | 下单用户 |
| `product_id` | BIGINT UNSIGNED | 商品 ID |
| `quantity` | INT UNSIGNED | 购买数量 |
| `unit_price` | DECIMAL(18,2) | **下单时快照单价**，不跟随商品改价 |
| `total_amount` | DECIMAL(18,2) | 订单总额 = `unit_price * quantity` |
| `status` | TINYINT | 0 待支付 1 已支付 2 已完成 3 已取消 4 已退款 |
| `expire_time` | DATETIME | 支付超时时间，定时任务据此关单 |
| `paid_at` | DATETIME | 支付完成时间，可空 |

索引：`uk_order_no`、`uk_request_id`、`idx_user_status(user_id, status)`、`idx_status_expire(status, expire_time)`。
状态机（单向，禁止回退）：`0 → 1 → 2`，`0 → 3`，`1 → 4`。状态流转必须带上原状态作为条件：`WHERE order_no = ? AND status = 0`，靠受影响行数判断是否被别人抢先改过。

### 5.5 `t_payment` —— 支付记录表

| 字段 | 类型 | 说明 |
|---|---|---|
| `payment_no` | VARCHAR(32) | 支付单号，**唯一索引** |
| `order_no` | VARCHAR(32) | 关联订单号，索引 |
| `channel` | VARCHAR(16) | `ALIPAY` / `WECHAT` / `MOCK` |
| `channel_trade_no` | VARCHAR(64) | 渠道交易号，**与 channel 组成联合唯一索引** |
| `amount` | DECIMAL(18,2) | 渠道回调金额 |
| `status` | TINYINT | 0 待支付 1 成功 2 失败 |
| `callback_time` | DATETIME | 渠道回调时间 |
| `raw_body` | TEXT | 回调原文，保留用于排查与对账举证 |

索引：`uk_payment_no`、`uk_channel_trade(channel, channel_trade_no)`、`idx_order_no`。
约定：同一个 `channel_trade_no` 重复回调是**正常现象**，靠联合唯一索引 + `INSERT IGNORE` / 捕获重复键异常来保证只落一条。

### 5.6 `t_idempotent` —— 幂等记录表

| 字段 | 类型 | 说明 |
|---|---|---|
| `request_id` | VARCHAR(64) | **唯一索引**，幂等键 |
| `user_id` | BIGINT UNSIGNED | 请求发起人 |
| `api_path` | VARCHAR(128) | 接口路径 |
| `request_hash` | CHAR(64) | 请求体 SHA-256，用于检测「同 ID 不同内容」的异常调用 |
| `response_body` | TEXT | 首次成功后的响应快照，重复请求原样返回 |
| `status` | TINYINT | 0 处理中 1 成功 2 失败 |
| `expire_time` | DATETIME | 过期时间，定时任务清理，索引 |

索引：`uk_request_id`、`idx_expire_time`。
约定：先 `INSERT` 抢占（唯一键冲突即「处理中」）→ 业务处理 → `UPDATE` 写回响应快照。**冲突且 status 仍为 0 时返回 `10003` 让客户端稍后重试，不要返回成功** —— 否则会在业务尚未完成时给出虚假成功。

### 5.7 `t_local_message` —— 本地消息表

| 字段 | 类型 | 说明 |
|---|---|---|
| `message_id` | VARCHAR(64) | 消息唯一 ID，**唯一索引**，也是消费端幂等键 |
| `biz_type` | VARCHAR(32) | 业务类型，如 `ORDER_PAID` |
| `biz_id` | VARCHAR(64) | 业务主键，如 `order_no` |
| `exchange` / `routing_key` | VARCHAR(128) | 投递目标 |
| `payload` | TEXT | 消息体 JSON |
| `status` | TINYINT | 0 待发送 1 已发送 2 已确认 3 发送失败 |
| `retry_count` | INT UNSIGNED | 已重试次数 |
| `next_retry_time` | DATETIME | 下次重试时间，**联合索引** |

索引：`uk_message_id`、`idx_status_retry(status, next_retry_time)`。
约定：**必须与业务写操作在同一事务内 INSERT**；`MessageRelayTask` 扫描 `status = 0` 且 `next_retry_time <= NOW()` 的记录投递；收到 publisher confirm 后置 `status = 2`；`retry_count` 超上限置 `3` 并告警。

### 5.8 `t_notify_record` —— 下游通知记录表

| 字段 | 类型 | 说明 |
|---|---|---|
| `notify_no` | VARCHAR(32) | 通知单号，**唯一索引** |
| `order_no` | VARCHAR(32) | 关联订单号，索引 |
| `target_url` | VARCHAR(255) | 商户回调地址 |
| `request_body` / `response_body` | TEXT | 请求与响应原文 |
| `status` | TINYINT | 0 待通知 1 成功 2 失败 3 放弃 |
| `retry_count` | INT UNSIGNED | 重试次数 |
| `next_retry_time` | DATETIME | 下次重试时间 |
| `message_id` | VARCHAR(64) | 来源消息 ID，**唯一索引**（消费幂等） |

索引：`uk_notify_no`、`uk_message_id`、`idx_status_retry(status, next_retry_time)`。
重试策略：指数退避 `2^n` 秒（1s / 2s / 4s / 8s / 16s…），上限 8 次后置 `3`。

### 5.9 `t_reconcile_batch` —— 对账批次表

| 字段 | 类型 | 说明 |
|---|---|---|
| `batch_no` | VARCHAR(32) | 批次号，**唯一索引** |
| `bill_date` | DATE | 账单日期，与 `channel` 组成联合唯一索引（防同一天重复对账） |
| `channel` | VARCHAR(16) | 渠道 |
| `total_count` / `total_amount` | INT / DECIMAL(18,2) | 本地侧汇总 |
| `channel_count` / `channel_amount` | INT / DECIMAL(18,2) | 渠道侧汇总 |
| `diff_count` | INT UNSIGNED | 差异笔数 |
| `status` | TINYINT | 0 处理中 1 已平账 2 有差异 3 失败 |

索引：`uk_batch_no`、`uk_date_channel(bill_date, channel)`。

### 5.10 `t_reconcile_diff` —— 对账差异明细表

| 字段 | 类型 | 说明 |
|---|---|---|
| `batch_no` | VARCHAR(32) | 所属批次，索引 |
| `order_no` | VARCHAR(32) | 订单号，索引 |
| `channel_trade_no` | VARCHAR(64) | 渠道交易号，可空 |
| `local_amount` / `channel_amount` | DECIMAL(18,2) | 两侧金额 |
| `diff_type` | TINYINT | 1 本地多单 2 渠道多单 3 金额不一致 4 状态不一致 |
| `diff_amount` | DECIMAL(18,2) | 差额 = 本地 − 渠道 |
| `handle_status` | TINYINT | 0 待处理 1 已核销 2 已忽略 |
| `handle_remark` / `handled_at` | VARCHAR(255) / DATETIME | 处理备注与时间 |

索引：`idx_batch(batch_no)`、`idx_order_no`、`idx_handle(handle_status)`。

---

## 6. 已知坑点

以下每一条都是真实会踩并且会导致**资损或数据不一致**的，改相关代码前先读这里。

### 6.1 防超卖：必须用条件 UPDATE，绝不能 SELECT 后再 UPDATE

**错误写法**（经典 TOCTOU，压测必现超卖）：

```cpp
// ❌ 两个线程都读到 stock=1，都判定充足，都扣减 → 库存变成 -1
auto p = productDao_->getById(tx, pid);
if (p.stock >= qty) {
    productDao_->updateStock(tx, pid, p.stock - qty);
}
```

**正确写法**（把判定与扣减合并成一条原子 SQL，靠 InnoDB 行锁 + 受影响行数）：

```sql
UPDATE t_product
   SET stock = stock - #{qty}, updated_at = NOW()
 WHERE id = #{pid}
   AND stock >= #{qty}
   AND status = 1;
```

```cpp
// ✅ affectedRows 才是唯一可信的判据
auto affected = productDao_->deductStock(tx, pid, qty);
if (affected == 0) {
    throw BizException(ErrCode::kStockNotEnough, "库存不足");
}
```

要点：

- **不要在 SQL 里先 `SELECT ... FOR UPDATE` 再判断** —— 能防超卖，但持有间隙锁会显著放大锁冲突与死锁概率，条件 UPDATE 更简单更快。
- **`affectedRows == 0` 有两种可能**：库存不足或商品下架。若要区分，扣减失败后再补一次查询给用户友好提示，但**判定依据永远是 `affectedRows`**，不是那次补查的结果。
- **不要用 `stock > 0` 代替 `stock >= qty`** —— 买 2 件时前者会漏判。
- `stock` 列**不要加 `UNSIGNED`** 之外的非负约束就以为万事大吉；列上加 `CHECK (stock >= 0)` 只是最后兜底，真正防超卖的是 `WHERE stock >= qty`。
- 乐观锁 `version` 是**辅助手段**，高并发下重试成本随冲突率指数上升，不要用它替代条件更新。它更适合账户余额这类「必须读到旧值才能算新值」的场景。
- 库存扣减必须在**进入支付流程之前**，且与订单创建在**同一事务**内。超时关单时要**把库存加回去**，这一步同样要幂等。

### 6.2 RabbitMQ 必须手动 ACK

**错误写法**：

```cpp
// ❌ 消息一投递就被确认，后续处理失败消息永久丢失
consumer->setAutoAck(true);   // 或在 consume 时传 autoAck = true
```

**正确写法**：

```cpp
// ✅ 处理成功才 ack；失败 nack 且 requeue=false，交给死信队列
channel->consume(exchange, routingKey, /*autoAck=*/false)
    .onMessage([&](const AMQP::Message& msg, uint64_t deliveryTag, bool redelivered) {
        try {
            handle(msg.body(), msg.bodySize());     // 内部必须幂等
            channel->ack(deliveryTag);
        } catch (const RetryableError&) {
            channel->nack(deliveryTag, /*requeue=*/false);   // 走 DLX
        } catch (const std::exception& e) {
            log->error("consume failed: {}", e.what());
            channel->nack(deliveryTag, /*requeue=*/false);
        }
    });
```

要点：

- `requeue = true` 要**慎用**：失败消息会被立刻重新投递，形成无限重试打满 CPU，且排在其他消息前面造成饥饿。**统一 `requeue = false` + 死信队列 + 死信消费者人工/延迟补偿**。
- **ACK 必须用当前消息的 `deliveryTag`**，不要缓存或跨 channel 复用。`deliveryTag` 是 channel 级别的，只在单个 channel 内有效。
- **消费者必须幂等**：手动 ACK 只能保证「至少一次」，重复投递是必然发生的（网络抖动、消费者重启、nack 后重投）。以 `message_id` 建唯一索引去重，重复消息直接 ack 丢弃。
- 消费逻辑要设**超时**。长事务/长阻塞会导致心跳超时，Broker 认为消费者死亡并重投消息。
- **不要在 `onMessage` 回调里做重活**，尤其是同步 HTTP 调用。回调线程被阻塞会拖垮整个 consumer。
- 连接断开要**自动重连并恢复消费者注册**，框架层封装好，业务代码不感知。

### 6.3 本地消息表必须与业务操作同事务

**错误写法**：

```cpp
// ❌ 事务提交后再发 MQ —— 提交与发送之间宕机，消息永久丢失
tx->commit();
mqClient->publish(msg);
```

```cpp
// ❌ 事务内发 MQ —— MQ 已收到但事务回滚，产生脏消息、下游拿到不存在的订单
tx->begin();
orderDao_->insert(tx, order);
mqClient->publish(msg);
tx->commit();
```

**正确写法**：业务写入与消息记录**在同一个事务内落库**，投递动作推迟到事务提交之后，由独立线程异步完成。

```cpp
// ✅ 第一步：同事务写业务数据 + 本地消息表
tx->begin();
orderDao_->insert(tx, order);
localMessageDao_->insert(tx, {
    .messageId_  = IdGenerator::snowflake(),
    .bizType_    = "ORDER_PAID",
    .bizId_      = order.orderNo_,
    .exchange_   = "trade.exchange",
    .routingKey_ = "order.paid",
    .payload_    = order.toJson().toStyledString(),
    .status_     = 0,            // 待发送
    .nextRetryTime_ = now()
});
tx->commit();                    // 业务与消息一起原子落库

// ✅ 第二步：MessageRelayTask 独立扫描并投递（与上一步解耦）
//    SELECT ... WHERE status = 0 AND next_retry_time <= NOW() LIMIT 200
//    投递成功且收到 publisher confirm → UPDATE status = 2
//    失败 → retry_count++，next_retry_time 按指数退避后移；超限置 3 并告警
```

要点：

- **绝不在事务内做 MQ 投递、HTTP 调用、Redis 之外的任何外部 IO**。外部系统的成功/失败无法与本地事务原子提交，二者之间必然存在不一致窗口。
- 本地消息表提供的是**最终一致性（至少一次投递）**，代价是**下游必须幂等**。这两件事是一套的，不能只做一半。
- 扫描任务要用 `SELECT ... FOR UPDATE SKIP LOCKED`（或 Redis 分布式锁）**防多实例重复投递**，否则多副本部署会重复发消息。
- 投递成功判定以 **publisher confirm** 为准，不是 `publish()` 返回不抛异常就算成功 —— 异步 confirm 模式下，消息可能根本没到 Broker。
- 消息表会持续增长，**必须有过期清理**（已确认 N 天后归档），否则拖慢扫描。清理不要和投递任务抢锁。

### 6.4 `ORDER` 是 MySQL 保留字

表名不要叫 `order`，否则必须处处加反引号，总有一次会忘：

```sql
-- ❌ 语法错误
SELECT * FROM order WHERE id = 1;
-- ✅ 统一用 t_order，全程无需转义
SELECT * FROM t_order WHERE id = 1;
```

ORM 映射与 `CREATE TABLE` 同理。**本项目的表名统一加 `t_` 前缀**，既是规避保留字，也便于与视图/临时表区分。

### 6.5 金额一律不用浮点

```cpp
// ❌ 0.1 + 0.2 != 0.3，累加十万笔后差额可能到几十元
double total = unit_price * quantity;
```

- 存储用 `DECIMAL(18,2)`，或在应用层统一用 **BIGINT 存「分」**。
- **DAO 返回的 DECIMAL 不要经过 `double` 中转**。要么让驱动以字符串/定点类型返回，要么在 SQL 里 `CAST(... AS SIGNED)` 直接取分。
- 金额比较用**整数分对比**或带容差的比较，禁止 `==` 直接比 `double`。
- 对外展示时才转成元，转换逻辑收敛到 `MoneyUtil`，不要在业务代码里到处 `/ 100.0`。

### 6.6 Drogon 异步回调与对象生命周期

Drogon 全异步，回调可能在请求对象析构之后才执行。**回调里捕获 `this` 或裸引用，会在高并发下随机崩溃。**

```cpp
// ❌ this 可能已被析构，回调触发时是野指针
dbClient->execSqlAsync(sql, [this](const Result& r) { this->handle(r); }, ...);

// ✅ 用 shared_ptr 捕获，或捕获值拷贝
dbClient->execSqlAsync(sql, [self = shared_from_this()](const Result& r) { ... }, ...);
```

- Controller 必须继承 `enable_shared_from_this` 并用 `shared_from_this()` 捕获。
- **注意循环引用**：`shared_ptr` 捕获进回调、回调又被对象持有 → 引用计数永不归零、内存泄漏。需要时用 `weak_ptr`。
- 事务对象（`TransactionPtr`）在提交/回滚后即失效，**不要在延迟任务里持有它**。
- 事务必须保证**任何异常路径都会回滚**，用 RAII 封装，不要手写 `try/catch` 分支。

### 6.7 Windows 宿主机 + Docker 的换行符与时区

- Git 在 Windows 上默认 `core.autocrlf=true`，会把 `.sh` 脚本改成 CRLF，导致容器内 `bash: \r: command not found`。**必须在仓库根加 `.gitattributes`**：

  ```
  * text=auto eol=lf
  *.sh text eol=lf
  *.sql text eol=lf
  ```

- 容器默认 UTC，与国内渠道账单日期对不上，**对账会整体错位一天**。在 `docker-compose.yml` 为每个服务显式设置：

  ```yaml
  environment:
    - TZ=Asia/Shanghai
  ```

  MySQL 端同样要把时区参数带上，JDBC/Drogon 连接串加 `charset=utf8mb4`，MySQL 8 默认排序规则已是 `utf8mb4_0900_ai_ci`，但**连接字符集仍需显式声明**，否则中文商品名可能乱码。
- 挂载源码进容器时，Windows 文件系统 IO 很慢。**构建产物 `build/` 用 named volume 而非 bind mount**，否则编译慢到不可用。
- 改 DDL 后必须 `docker compose down -v`，`/docker-entrypoint-initdb.d` 里的脚本**只在数据卷为空时执行一次**。

### 6.8 定时任务重入

`MessageRelayTask` / `NotifyRetryTask` / `OrderTimeoutTask` 都是「扫描 → 处理」模式，天然不幂等。

- 单实例也要**防重入**：上一轮没跑完，下一轮定时器又触发了，同一批数据会被处理两次。
- 多实例部署必须加 **Redis 分布式锁**（`SET key value NX PX 30000`），带超时和续期，锁的 value 用唯一 ID，**释放时校验是自己的锁**，不要直接 `DEL`。
- 任务内**逐条 try/catch**，一条失败不能中断整批。
- 批量处理要**分页或 LIMIT**，避免一次扫全表把内存打满。

### 6.9 对账的「日期归属」问题

对账最容易被问倒的地方，提前想清楚：

- 一笔支付在 **23:59:58** 发起、渠道侧记录为 **次日 00:00:01**，按哪天的账算？**以渠道账单为准**是通行做法，本地侧要按渠道回调时间归集，而不是本地创建时间。
- 因此 `t_payment` 必须**单独存 `callback_time`**，不要图省事用 `created_at` 当对账口径。
- 对账跑之前要确认**当日订单已全部终态化**（超时单已关单），否则会把正常的「处理中」订单误判成「本地多单」。
- 差异类型 `1 本地多单` 的常见真因不是资损，而是**渠道账单延迟**。所以对账任务要支持**对历史日期重跑**，且重跑要能覆盖/更新已有批次结果。

---

## 7. 参考文档

- 接口清单与请求响应示例：`docs/api.md`
- 错误码全表：`docs/error_code.md`
- 关键设计决策与取舍（**面试前重点看这里**）：`docs/design.md`
