# 测试目标，由顶层 CMakeLists.txt 在 TRADE_BUILD_TESTS=ON 时 add_subdirectory 进来。
# 产物 build/<preset>/tests/unit_tests。

# 与 src/ 同样：GLOB 必须带 CONFIGURE_DEPENDS，否则新增的测试文件不会被感知
file(GLOB_RECURSE TRADE_UNIT_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cc")

# 集成测试（连真实 MySQL / Redis）将来单独建目标，不混进 unit_tests：
# 单测要人人随时能跑，集成测试要先起中间件。
# file(GLOB_RECURSE TRADE_INTEGRATION_TEST_SOURCES CONFIGURE_DEPENDS
#     "${CMAKE_CURRENT_SOURCE_DIR}/integration/*.cc")

if(NOT TRADE_UNIT_TEST_SOURCES)
    message(WARNING "tests/unit/ 下没有源文件，跳过 unit_tests 目标。")
    return()
endif()

add_executable(unit_tests ${TRADE_UNIT_TEST_SOURCES})

# trade_core 提供业务代码与 include 路径；gtest_main 提供 main()，
# 并会传递地带上 gtest，不用再单独链接 GTest::gtest。
# 与 trade_server 同理：whole-archive 保证静态库中未被直接引用的
# 目标文件（自注册类等）也被纳入。
target_link_libraries(unit_tests PRIVATE
    -Wl,--whole-archive trade_core -Wl,--no-whole-archive
    GTest::gtest_main)

# 注册到 CTest
#
# 用 gtest_discover_tests 而不是 add_test(NAME unit_tests COMMAND unit_tests)：
# 后者只注册一个名为 unit_tests 的用例，`ctest -R 用例名` 匹配不到；
# 前者在构建期扫描可执行文件，把每个 TEST(...) 注册成独立用例，
# 才支持按名称过滤、并行执行、单独重跑失败的用例。
#
# 将来交叉编译（构建期跑不了目标平台的二进制）时换成：
#   add_test(NAME unit_tests COMMAND unit_tests)
# 代价是失去按用例粒度的能力。
include(GoogleTest)

gtest_discover_tests(unit_tests
    DISCOVERY_TIMEOUT 30
    PROPERTIES
        LABELS "unit"
        TIMEOUT 60)
