# 多阶段构建:builder → runtime,用于 docker compose 的一键部署演示。
# 日常开发不走这里,app 在 Dev Container 内编译运行。
# 依赖清单与 .devcontainer/devcontainer.json 保持同步,改一处必须改另一处。

# ---------------------------------------------------------------------------
# 构建阶段
# ---------------------------------------------------------------------------
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build git curl ca-certificates gnupg \
        gdb pkg-config libssl-dev zlib1g-dev uuid-dev libjsoncpp-dev \
        libhiredis-dev librabbitmq-dev libc-ares-dev \
        libbrotli-dev libspdlog-dev libgtest-dev libpq-dev libmariadb-dev \
    && ln -sfn /usr/include/mariadb /usr/include/mysql \
    && { (curl -fsSL https://packagecloud.io/install/repositories/drogonframework/drogon/script.deb.sh | bash \
           && apt-get install -y --no-install-recommends drogon) \
         || (cd /tmp \
             && git clone --depth 1 --branch v1.9.13 https://github.com/drogonframework/drogon.git \
             && cd drogon \
             && git submodule update --init --recursive \
             && mkdir -p build && cd build \
             && cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
                  -DBUILD_EXAMPLES=OFF -DBUILD_CTL=OFF -DBUILD_TESTING=OFF \
                  -DBUILD_DOC=OFF -DBUILD_REDIS=ON -DBUILD_MYSQL=ON -DBUILD_SQLITE=OFF \
             && make -j"$(nproc)" && make install && ldconfig \
             && rm -rf /tmp/drogon); } \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .
# release 预设:关测试、关 trace 日志(CMakePresets.json)
RUN cmake --preset release \
    && cmake --build build/release -j"$(nproc)"

# ---------------------------------------------------------------------------
# 运行阶段
# ---------------------------------------------------------------------------
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive \
    TZ=Asia/Shanghai \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8

# 装 drogon 会连带拉齐运行时依赖;tzdata 保证 TZ 生效
RUN apt-get update && apt-get install -y --no-install-recommends \
        drogon tzdata \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /build/build/release/trade_server /app/trade_server
COPY config/config.json /app/config/config.json

EXPOSE 8080
CMD ["/app/trade_server", "--config", "/app/config/config.json"]
