# ============================================================================
# 高可靠交易与对账系统 —— 顶层构建入口
#
# 约定见 CLAUDE.md 第 3 节：所有构建命令在 Dev Container (Ubuntu 22.04) 内执行。
#   cmake --preset debug
#   cmake --build build/debug -j$(nproc)
# ============================================================================
cmake_minimum_required(VERSION 3.20)

project(trade_reconcile_system
    VERSION     0.1.0
    DESCRIPTION "高可靠交易与对账系统 / High-reliability trade & reconciliation system"
    LANGUAGES   CXX)

# ---------------------------------------------------------------------------
# 语言标准
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD          17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)   # 关掉 gnu++17 扩展，代码在 GCC/Clang 下行为一致

# 单配置生成器下若未指定构建类型，默认 Debug（而不是空字符串，空字符串不带任何优化也不带调试信息）
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "构建类型" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# 生成 compile_commands.json，供 VSCode C++ 插件 / clangd 使用
# 仅对 Makefile / Ninja 生成器有效（MSVC 生成器不支持）
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 禁止 in-source build：否则 CMakeCache.txt 会散落在源码树里，污染 git status
if(CMAKE_BINARY_DIR STREQUAL CMAKE_SOURCE_DIR)
    message(FATAL_ERROR
        "禁止在源码目录内构建。\n"
        "请使用:  cmake --preset debug  ")
endif()

# ---------------------------------------------------------------------------
# 构建选项
# ---------------------------------------------------------------------------
option(TRADE_BUILD_TESTS      "编译 GoogleTest 单元/集成测试"        ON)
option(TRADE_ENABLE_TRACE_LOG "编译期开启 spdlog trace 级别（见 CLAUDE.md 4.4）" OFF)
option(TRADE_ENABLE_SANITIZER "开启 ASan + UBSan，排查内存与未定义行为"         OFF)

# ---------------------------------------------------------------------------
# 第三方依赖
# ---------------------------------------------------------------------------
# Drogon：包管理装好后提供 Drogon::Drogon 目标
find_package(Drogon   REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB    REQUIRED)
find_package(Jsoncpp REQUIRED)   # 提供 Jsoncpp::jsoncpp
find_package(spdlog  REQUIRED)   # 提供 spdlog::spdlog
find_package(Threads REQUIRED)

# MySQL / hiredis / rabbitmq-c 在 Ubuntu 上没有靠谱的 CMake config 文件，
# 统一走 pkg-config 解析。
#
# 若这里报 not found，先用下面命令确认包名（不同发行版 pc 文件名不一样）：
#   pkg-config --list-all | grep -Ei 'mysql|hiredis|rabbit'
# 常见对应关系（Ubuntu 22.04）：
#   libmysqlclient-dev → mysqlclient.pc
#   libhiredis-dev     → hiredis.pc
#   librabbitmq-dev    → librabbitmq.pc
find_package(PkgConfig REQUIRED)
pkg_check_modules(MYSQLCLIENT REQUIRED IMPORTED_TARGET mysqlclient)
pkg_check_modules(HIREDIS     REQUIRED IMPORTED_TARGET hiredis)
pkg_check_modules(RABBITMQ    REQUIRED IMPORTED_TARGET librabbitmq)

# ---------------------------------------------------------------------------
# 源文件收集
# ---------------------------------------------------------------------------
# ⚠️ GLOB 的经典陷阱：CMake 只在 configure 阶段展开 glob，之后新增的源文件
#    不会被感知，你会对着「符号未定义」查半天，最后发现是没重新 configure。
#    CONFIGURE_DEPENDS 让构建系统在每次构建时检查 glob 是否失效并自动重跑，
#    这是使用 GLOB 时**必须**加的参数。
file(GLOB_RECURSE TRADE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

if(NOT TRADE_SOURCES)
    message(FATAL_ERROR
        "src/ 下没有收集到任何源文件。\n"
        "请先写入第一批源码（至少 src/main.cc），再执行 cmake --preset debug。")
endif()

# 定位入口文件
set(TRADE_MAIN "")
foreach(_candidate "src/main.cc" "src/main.cpp")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_candidate}")
        set(TRADE_MAIN "${CMAKE_CURRENT_SOURCE_DIR}/${_candidate}")
        break()
    endif()
endforeach()

if(NOT TRADE_MAIN)
    message(FATAL_ERROR "找不到入口文件 src/main.cc 或 src/main.cpp")
endif()

# main 单独编，其余业务代码编成静态库。
#
# 这么拆不是为了好看，是为了让 tests/ 能链接业务代码：
# 若把所有 .cc 塞进一个可执行目标，GTest 测试链接时就必然撞上
# 「multiple definition of main」—— 因为 src/main.cc 和 gtest_main 都有 main()。
list(FILTER TRADE_SOURCES EXCLUDE REGEX "/main\\.(cc|cpp)$")

# ---------------------------------------------------------------------------
# 统一的编译选项（INTERFACE 库，自动传播到所有下游目标，测试目标也吃得到）
# ---------------------------------------------------------------------------
add_library(trade_build_flags INTERFACE)

# 告警：CLAUDE.md 要求 -Wall -Wextra
target_compile_options(trade_build_flags INTERFACE
    -Wall
    -Wextra)

# Debug：显式加 -g -O0
#   -fno-omit-frame-pointer 是为 gdb 回溯和 perf 火焰图准备的，
#   缺了它栈回溯只有一串问号，排查线上问题会非常痛苦。
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(trade_build_flags INTERFACE
        -g
        -O0
        -fno-omit-frame-pointer)
endif()

if(TRADE_ENABLE_SANITIZER)
    target_compile_options(trade_build_flags INTERFACE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer)
    target_link_options(trade_build_flags INTERFACE
        -fsanitize=address,undefined)
endif()

if(TRADE_ENABLE_TRACE_LOG)
    # CLAUDE.md 4.4：trace 级日志只在 debug 构建开启。
    # spdlog 在编译期就会把低于该级别的日志整段裁掉，零运行时开销。
    target_compile_definitions(trade_build_flags INTERFACE
        SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
endif()

# ---------------------------------------------------------------------------
# 业务核心库
# ---------------------------------------------------------------------------
add_library(trade_core STATIC ${TRADE_SOURCES})

target_include_directories(trade_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(trade_core PUBLIC
    trade_build_flags
    Drogon::Drogon
    PkgConfig::MYSQLCLIENT
    PkgConfig::HIREDIS
    PkgConfig::RABBITMQ
    Jsoncpp::jsoncpp
    spdlog::spdlog
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
    Threads::Threads)

# ---------------------------------------------------------------------------
# 可执行文件
# 产物路径与 CLAUDE.md 第 3 节一致：build/debug/trade_server
# ---------------------------------------------------------------------------
add_executable(trade_server "${TRADE_MAIN}")
target_link_libraries(trade_server PRIVATE trade_core)

# ---------------------------------------------------------------------------
# 测试
# ---------------------------------------------------------------------------
if(TRADE_BUILD_TESTS)
    # 先探测文件是否存在，避免第一批代码还没写时 cmake 直接 configure 失败。
    # 这不是「静默跳过」—— 会打 WARNING，看得见。
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        enable_testing()
        add_subdirectory(tests)
    else()
        message(WARNING
            "TRADE_BUILD_TESTS=ON，但 tests/CMakeLists.txt 不存在，已跳过测试目标。\n"
            "补上该文件后重新 configure 即可（cmake --preset debug）。")
    endif()
endif()

# ---------------------------------------------------------------------------
# 配置摘要
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "================= trade-reconcile-system =================")
message(STATUS "  构建类型    : ${CMAKE_BUILD_TYPE}")
message(STATUS "  编译器      : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  构建目录    : ${CMAKE_BINARY_DIR}")
message(STATUS "  核心库源码  : ${CMAKE_CURRENT_SOURCE_DIR}/src")
message(STATUS "  编译测试    : ${TRADE_BUILD_TESTS}")
message(STATUS "  Trace 日志  : ${TRADE_ENABLE_TRACE_LOG}")
message(STATUS "  Sanitizer   : ${TRADE_ENABLE_SANITIZER}")
message(STATUS "  MySQL       : ${MYSQLCLIENT_VERSION}")
message(STATUS "  hiredis     : ${HIREDIS_VERSION}")
message(STATUS "  rabbitmq-c  : ${RABBITMQ_VERSION}")
message(STATUS "==========================================================")
message(STATUS "")
