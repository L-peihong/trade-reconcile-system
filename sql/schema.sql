-- ============================================================================
-- 高可靠交易与对账系统 —— 建表 DDL
--
-- 挂载于 MySQL 容器的 /docker-entrypoint-initdb.d，仅在数据卷为空时执行一次。
-- 改了本文件要 `docker compose down -v` 清卷重建（CLAUDE.md 3.1 / 6.7）。
--
-- 规范（CLAUDE.md 第 5 节）：
--   库名 trade_reconcile；引擎 InnoDB；字符集 utf8mb4 / utf8mb4_0900_ai_ci；
--   表名统一 t_ 前缀（规避 ORDER 保留字，CLAUDE.md 6.4）；
--   金额一律 DECIMAL(18,2)，禁 FLOAT/DOUBLE（CLAUDE.md 6.5）；
--   CHECK 约束只是兜底（CLAUDE.md 6.1），防超卖靠条件 UPDATE 的 WHERE。
-- 10 张表全部属于 V1；标注 V1.5 的字段只留列结构，不实现对应功能。
-- ============================================================================

CREATE DATABASE IF NOT EXISTS trade_reconcile
    DEFAULT CHARACTER SET utf8mb4
    DEFAULT COLLATE utf8mb4_0900_ai_ci;

USE trade_reconcile;

SET NAMES utf8mb4;

-- ---------------------------------------------------------------------------
-- 驱动兼容（2026-09-13 实踩）：Drogon 的 MySQL 客户端（容器内是 MariaDB
-- 客户端库）未启用服务器公钥获取/TLS（MysqlConnection.cc:62-64 只有
-- NONBLOCK/RECONNECT），连不上 caching_sha2_password 的完整认证，表现为
-- 事务等连接超时（"Timeout, no connection available for transaction"）。
-- 开发环境统一用 mysql_native_password（MySQL 8 仍支持，仅弃用告警）。
-- 前提：docker-compose.yml 已设 MYSQL_ROOT_HOST="%"（init 时创建 root@'%'）。
-- ---------------------------------------------------------------------------
ALTER USER 'root'@'%' IDENTIFIED WITH mysql_native_password BY 'root123';

-- ---------------------------------------------------------------------------
-- 5.1 t_account 账户表
-- V1 只做入账/出账；冻结/解冻（frozen_amount、status=1）属 V1.5，字段预留。
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS t_account (
    id            BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    account_no    VARCHAR(32)    NOT NULL COMMENT '账户号',
    user_id       BIGINT UNSIGNED NOT NULL COMMENT '用户ID',
    balance       DECIMAL(18,2)  NOT NULL DEFAULT 0.00 COMMENT '可用余额',
    frozen_amount DECIMAL(18,2)  NOT NULL DEFAULT 0.00 COMMENT '冻结金额(V1.5预留)',
    version       INT UNSIGNED   NOT NULL DEFAULT 0 COMMENT '乐观锁版本号',
    status        TINYINT        NOT NULL DEFAULT 0 COMMENT '0正常 1冻结 2注销',
    created_at    DATETIME(3)    NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    updated_at    DATETIME(3)    NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3),
    UNIQUE KEY uk_account_no (account_no),
    UNIQUE KEY uk_user_id (user_id),
    KEY idx_status (status),
    CONSTRAINT chk_account_balance CHECK (balance >= 0)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci COMMENT='账户表';

-- ---------------------------------------------------------------------------
-- 5.2 t_account_flow 账户流水表
-- 流水表只增不改（无 updated_at），是日终对账的本地数据源。
-- uk_request_biz 防同一请求重复记账（幂等兜底）。
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS t_account_flow (
    id             BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    account_no     VARCHAR(32)   NOT NULL COMMENT '账户号',
    biz_type       TINYINT       NOT NULL COMMENT '1充值 2支付 3退款 4冻结 5解冻',
    direction      TINYINT       NOT NULL COMMENT '1入账 2出账',
    amount         DECIMAL(18,2) NOT NULL COMMENT '变动金额，恒为正，方向由 direction 表达',
    before_balance DECIMAL(18,2) NOT NULL COMMENT '变动前余额',
    after_balance  DECIMAL(18,2) NOT NULL COMMENT '变动后余额',
    order_no       VARCHAR(32)   NULL COMMENT '关联订单号',
    request_id     VARCHAR(64)   NOT NULL COMMENT '来源请求ID（幂等防重复记账）',
    created_at     DATETIME(3)   NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    UNIQUE KEY uk_request_biz (request_id, biz_type),
    KEY idx_account_time (account_no, created_at),
    CONSTRAINT chk_flow_amount CHECK (amount > 0)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci COMMENT='账户流水表（对账本地数据源）';

-- ---------------------------------------------------------------------------
-- 5.3 t_product 商品表
-- V1 直接扣减/归还 stock，不引入锁定库存（locked_stock 双栏设计属 V1.5）。
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS t_product (
    id         BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    product_no VARCHAR(32)    NOT NULL COMMENT '商品编码',
    name       VARCHAR(128)   NOT NULL COMMENT '商品名称',
    price      DECIMAL(18,2)  NOT NULL COMMENT '单价（元）',
    stock      INT UNSIGNED   NOT NULL DEFAULT 0 COMMENT '可售库存',
    version    INT UNSIGNED   NOT NULL DEFAULT 0 COMMENT '乐观锁版本号（辅助手段）',
    status     TINYINT        NOT NULL DEFAULT 1 COMMENT '0下架 1上架',
    created_at DATETIME(3)    NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    updated_at DATETIME(3)    NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3),
    UNIQUE KEY uk_product_no (product_no),
    KEY idx_status (status),
    CONSTRAINT chk_product_stock CHECK (stock >= 0)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci COMMENT='商品表';

-- ---------------------------------------------------------------------------
-- 5.4 t_order 订单表
-- 表名用 t_order 而非 order：ORDER 是 MySQL 保留字（CLAUDE.md 6.4）。
-- uk_request_id 是幂等兜底（与 t_idempotent 双保险）。
-- 状态机（单向禁止回退）：V1 实现 0→1→2 与 0→3（超时关单归还库存）；1→4 退款属 V1.5。
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS t_order (
    id           BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    order_no     VARCHAR(32)   NOT NULL COMMENT '业务订单号（雪花）',
    request_id   VARCHAR(64)   NOT NULL COMMENT '创建请求ID（幂等兜底）',
    user_id      BIGINT UNSIGNED NOT NULL COMMENT '下单用户',
    product_id   BIGINT UNSIGNED NOT NULL COMMENT '商品ID',
    quantity     INT UNSIGNED  NOT NULL COMMENT '购买数量',
    unit_price   DECIMAL(18,2) NOT NULL COMMENT '下单时快照单价，不跟随改价',
    total_amount DECIMAL(18,2) NOT NULL COMMENT '订单总额 = 单价 × 数量',
    status       TINYINT       NOT NULL DEFAULT 0 COMMENT '0待支付 1已支付 2已完成 3已取消 4已退款',
    expire_time  DATETIME      NOT NULL COMMENT '支付超时时间，超时关单归还库存',
    paid_at      DATETIME      NULL COMMENT '支付完成时间',
    created_at   DATETIME(3)   NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    updated_at   DATETIME(3)   NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3),
    UNIQUE KEY uk_order_no (order_no),
    UNIQUE KEY uk_request_id (request_id),
    KEY idx_user_status (user_id, status),
    KEY idx_status_expire (status, expire_time),
    CONSTRAINT chk_order_quantity CHECK (quantity > 0),
    CONSTRAINT chk_order_amount CHECK (total_amount >= 0)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci COMMENT='订单表';

-- ---------------------------------------------------------------------------
-- 5.5 t_payment 支付记录表
-- 同一 channel_trade_no 重复回调是正常现象，uk_channel_trade 保证只落一条。
-- callback_time 是对账口径（CLAUDE.md 6.9），与 created_at 分离。
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS t_payment (
    id               BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    payment_no       VARCHAR(32)   NOT NULL COMMENT '支付单号',
    order_no         VARCHAR(32)   NOT NULL COMMENT '关联订单号',
    channel          VARCHAR(16)   NOT NULL COMMENT 'MOCK / ALIPAY / WECHAT',
    channel_trade_no VARCHAR(64)   NOT NULL COMMENT '渠道交易号',
    amount           DECIMAL(18,2) NOT NULL COMMENT '渠道回调金额',
    status           TINYINT       NOT NULL DEFAULT 0 COMMENT '0待支付 1成功 2失败',
    callback_time    DATETIME      NULL COMMENT '渠道回调时间（对账口径）',
    raw_body         TEXT          NULL COMMENT '回调原文，排查与对账举证',
    created_at       DATETIME(3)   NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    updated_at       DATETIME(3)   NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3),
    UNIQUE KEY uk_payment_no (payment_no),
    UNIQUE KEY uk_channel_trade (channel, channel_trade_no),
    KEY idx_order_no (order_no)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci COMMENT='支付记录表';

-- ---------------------------------------------------------------------------
-- 5.6 t_idempotent 幂等记录表
-- 语义边界见 CLAUDE.md 5.6：与业务同事务；status=0 冲突返回 10003；
-- 先到者失败后到者重走流程（业务自身幂等兜底）。
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS t_idempotent (
    id            BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    request_id    VARCHAR(64)  NOT NULL COMMENT '幂等键',
    user_id       BIGINT UNSIGNED NOT NULL COMMENT '请求发起人',
    api_path      VARCHAR(128) NOT NULL COMMENT '接口路径',
    request_hash  CHAR(64)     NOT NULL COMMENT '请求体 SHA-256，检测同ID不同内容',
    response_body TEXT         NULL COMMENT '首次成功后的响应快照',
    status        TINYINT      NOT NULL DEFAULT 0 COMMENT '0处理中 1成功 2失败',
    expire_time   DATETIME     NOT NULL COMMENT '过期时间，定时清理',
    created_at    DATETIME(3)  NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    updated_at    DATETIME(3)  NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3),
    UNIQUE KEY uk_request_id (request_id),
    KEY idx_expire_time (expire_time)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci COMMENT='幂等记录表';

-- ---------------------------------------------------------------------------
-- 5.7 t_local_message 本地消息表
-- 与业务写操作同事务 INSERT（CLAUDE.md 6.3），由 MessageRelayTask 投递 MQ。
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS t_local_message (
    id              BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    message_id      VARCHAR(64)  NOT NULL COMMENT '消息唯一ID（消费端幂等键）',
    biz_type        VARCHAR(32)  NOT NULL COMMENT '业务类型，如 ORDER_PAID',
    biz_id          VARCHAR(64)  NOT NULL COMMENT '业务主键，如 order_no',
    exchange        VARCHAR(128) NOT NULL COMMENT '投递交换机',
    routing_key     VARCHAR(128) NOT NULL COMMENT '投递路由键',
    payload         TEXT         NOT NULL COMMENT '消息体 JSON',
    status          TINYINT      NOT NULL DEFAULT 0 COMMENT '0待发送 1已发送 2已确认 3发送失败',
    retry_count     INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '已重试次数',
    next_retry_time DATETIME     NOT NULL COMMENT '下次重试时间',
    created_at      DATETIME(3)  NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    updated_at      DATETIME(3)  NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3),
    UNIQUE KEY uk_message_id (message_id),
    KEY idx_status_retry (status, next_retry_time)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci COMMENT='本地消息表';

-- ---------------------------------------------------------------------------
-- 5.8 t_notify_record 下游通知记录表
-- V1 重试由死信队列承担；uk_message_id 同时承担消费幂等去重。
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS t_notify_record (
    id              BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    notify_no       VARCHAR(32)  NOT NULL COMMENT '通知单号',
    order_no        VARCHAR(32)  NOT NULL COMMENT '关联订单号',
    target_url      VARCHAR(255) NOT NULL COMMENT '商户回调地址',
    request_body    TEXT         NULL COMMENT '通知请求原文',
    response_body   TEXT         NULL COMMENT '商户响应原文',
    status          TINYINT      NOT NULL DEFAULT 0 COMMENT '0待通知 1成功 2失败 3放弃',
    retry_count     INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '重试次数',
    next_retry_time DATETIME     NULL COMMENT '下次重试时间',
    message_id      VARCHAR(64)  NOT NULL COMMENT '来源消息ID（消费幂等）',
    created_at      DATETIME(3)  NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    updated_at      DATETIME(3)  NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3),
    UNIQUE KEY uk_notify_no (notify_no),
    UNIQUE KEY uk_message_id (message_id),
    KEY idx_status_retry (status, next_retry_time),
    KEY idx_order_no (order_no)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci COMMENT='下游通知记录表';

-- ---------------------------------------------------------------------------
-- 5.9 t_reconcile_batch 对账批次表
-- uk_date_channel 防同一天同渠道重复对账。
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS t_reconcile_batch (
    id             BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    batch_no       VARCHAR(32)   NOT NULL COMMENT '批次号',
    bill_date      DATE          NOT NULL COMMENT '账单日期',
    channel        VARCHAR(16)   NOT NULL COMMENT '渠道',
    total_count    INT UNSIGNED  NOT NULL DEFAULT 0 COMMENT '本地侧笔数',
    total_amount   DECIMAL(18,2) NOT NULL DEFAULT 0.00 COMMENT '本地侧总额',
    channel_count  INT UNSIGNED  NOT NULL DEFAULT 0 COMMENT '渠道侧笔数',
    channel_amount DECIMAL(18,2) NOT NULL DEFAULT 0.00 COMMENT '渠道侧总额',
    diff_count     INT UNSIGNED  NOT NULL DEFAULT 0 COMMENT '差异笔数',
    status         TINYINT       NOT NULL DEFAULT 0 COMMENT '0处理中 1已平账 2有差异 3失败',
    created_at     DATETIME(3)   NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    updated_at     DATETIME(3)   NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3),
    UNIQUE KEY uk_batch_no (batch_no),
    UNIQUE KEY uk_date_channel (bill_date, channel)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci COMMENT='对账批次表';

-- ---------------------------------------------------------------------------
-- 5.10 t_reconcile_diff 对账差异明细表
-- diff_type：1本地多单 2渠道多单 3金额不一致 4状态不一致
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS t_reconcile_diff (
    id                BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    batch_no          VARCHAR(32)   NOT NULL COMMENT '所属批次',
    order_no          VARCHAR(32)   NOT NULL COMMENT '订单号',
    channel_trade_no  VARCHAR(64)   NULL COMMENT '渠道交易号',
    local_amount      DECIMAL(18,2) NOT NULL DEFAULT 0.00 COMMENT '本地侧金额',
    channel_amount    DECIMAL(18,2) NOT NULL DEFAULT 0.00 COMMENT '渠道侧金额',
    diff_type         TINYINT       NOT NULL COMMENT '1本地多单 2渠道多单 3金额不一致 4状态不一致',
    diff_amount       DECIMAL(18,2) NOT NULL DEFAULT 0.00 COMMENT '差额 = 本地 − 渠道',
    handle_status     TINYINT       NOT NULL DEFAULT 0 COMMENT '0待处理 1已核销 2已忽略',
    handle_remark     VARCHAR(255)  NULL COMMENT '处理备注',
    handled_at        DATETIME      NULL COMMENT '处理时间',
    created_at        DATETIME(3)   NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    updated_at        DATETIME(3)   NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3),
    KEY idx_batch (batch_no),
    KEY idx_order_no (order_no),
    KEY idx_handle (handle_status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci COMMENT='对账差异明细表';
