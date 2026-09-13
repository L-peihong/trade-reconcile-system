# 高可靠交易与对账系统

> C++17 / Drogon / MySQL 8 / RabbitMQ / Docker Compose —— 一个把「钱不能算错、消息不能丢、重复请求不能重复扣款」做扎实的交易对账系统。

覆盖一笔交易的完整生命周期：**下单 → 支付回调 → 商户通知 → 日终对账 → 差异核销**。

## 四个深度亮点（均有验收数据）

| 亮点 | 实现 | 验收数据 |
|---|---|---|
| 防超卖 | 条件 UPDATE（`WHERE stock >= ?`）+ InnoDB 行锁，以 affectedRows 为唯一判定 | 100 并发抢 50 件库存：成功恰 50、拒绝恰 50、库存归零、**零超卖** |
| 接口幂等 | 唯一索引抢占 + 响应快照返回（`t_idempotent`），业务失败键随事务释放 | 同一 X-Request-Id 重放 N 次只生效一次，响应**逐字节一致** |
| 可靠消息 | 本地消息表与业务**同事务**写入 → 独立投递（publisher confirm）→ 手动 ACK 消费 → 幂等去重 → 死信兜底 | broker 宕机重试 5 轮指数退避后恢复送达；重复消息跳过；商户不可达 3 次后转死信队列 |
| 日终对账 | 渠道账单与本地流水按渠道交易号逐笔核对，差异自动分型，人工核销闭环 | 注入本地多单/渠道多单/金额不一致各 1 笔 → 恰好 3 笔差异、金额差分毫不差 |

## 架构

```
                ┌──────────────────────── trade_server (C++17 / Drogon) ────────────────────────┐
                │                                                                                │
 HTTP ──────────┼─► RequestIdFilter ─► Controller ─► ThreadPool ─► Service(事务/TransactionGuard)│
                │      (X-Request-Id)    (仅解析参数)    (同步事务)    └─► DAO(单表 SQL) ──┐       │
                │                                                                            │       │
                │  MessageRelayTask(5s) 扫描 t_local_message ── publish+confirm ──► RabbitMQ  │       │
                │  NotifyConsumer        ◄── 手动 ACK / 失败 nack ── 死信队列                │       │
                │  ReconcileTask(60s)    账单就绪且当日未对账 → 自动跑批                       │       │
                └────────────┬──────────────────────────────┬─────────────────────────────┘       │
                             │                              │                                     │
                    ┌────────▼────────┐            ┌────────▼────────┐                  ┌─────────▼────────┐
                    │  MySQL 8.0      │            │  RabbitMQ 3.12   │                  │  Redis 7(V1.5 预留)│
                    │  10 张表 / InnoDB│            │  topic + DLX     │                  │                   │
                    └─────────────────┘            └──────────────────┘                  └───────────────────┘
```

**核心设计**（详见 [CLAUDE.md](CLAUDE.md) 与 [docs/design.md](docs/design.md)）：

- 事务边界只在 Service 层，`TransactionGuard` RAII 守护：显式提交失败抛异常、异常路径必然回滚
- 同步 DB/MQ 一律在线程池执行 —— Drogon 事件循环线程禁止阻塞（已核对 v1.9.13 源码，fast 客户端下会死锁/assert）
- 金额全链路整数「分」运算，DECIMAL 边界转换收敛于 MoneyUtil，不经 double
- `expire_time`/对账日期显式 UTC+8，不依赖进程时区（实测容器 TZ 环境变量不可靠）

## 快速开始

```bash
# 宿主机（Windows + Docker Desktop）
docker compose up -d mysql redis rabbitmq     # 中间件
# VSCode → Reopen in Container                  # Dev Container(ubuntu 22.04,依赖自动装)

# 容器内
cmake --preset debug && cmake --build build/debug -j$(nproc)
./build/debug/trade_server

# 端到端演示(一键:下单→支付→通知→对账→核销)
bash scripts/smoke_test.sh

# 压测与单测
bash scripts/stress_stock.sh                   # 100 并发抢 50 库存
ctest --test-dir build/debug --output-on-failure   # 20+ 单元测试
```

## 技术栈

C++17 · Drogon 1.9 · MySQL 8.0 · RabbitMQ 3.12 · Redis 7 · Docker Compose · GoogleTest · spdlog · CMake/Ninja · rabbitmq-c · jsoncpp

## 目录

```
src/controllers   HTTP 层(参数解析/组装响应)
src/services      业务层(事务边界/编排/校验)
src/dao           数据访问层(单表 SQL,不开事务)
src/tasks         定时任务(消息投递/日终对账)
src/consumer      MQ 消费者(手动 ACK + 幂等)
src/common|utils  错误码/日志/过滤器/线程池/事务守卫/验签/雪花 ID
sql/              建表与种子数据(10 张表)
scripts/          冒烟/压测/账单生成
docs/             面试问答大纲(design.md)
```

## 面试前

[docs/design.md](docs/design.md) 按「问题 → 方案 → 权衡 → 验证数据」组织了每个技术决策的问答 —— 面试前按此复习。踩坑实录（Drogon 事务语义、MariaDB 客户端兼容、幂等快照格式……）在 [CLAUDE.md](CLAUDE.md) 第 6 节。
