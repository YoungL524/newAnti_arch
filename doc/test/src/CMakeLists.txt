# 单元测试可执行文件
# 仅在 BUILD_TESTS=ON 时构建（顶层 CMakeLists.txt 控制）
#
# 之所以放在 doc/test/src/ 是为了与业务文档/测试文档放在一起，
# 方便“一条业务线 = 一份文档 + 一份测试”的组织方式。
#
# 这里复用 core/business/ 下的源文件直接编译，避免引入额外的库目标。

# ----- RadarWorkModeController 测试 -----
add_executable(tst_RadarWorkModeController
    tst_RadarWorkModeController.cpp
    ${CMAKE_SOURCE_DIR}/core/business/RadarWorkModeController.cpp
    ${CMAKE_SOURCE_DIR}/core/business/RadarWorkModeController.h
    ${CMAKE_SOURCE_DIR}/core/business/IWorkModeCommandSender.h
)

target_include_directories(tst_RadarWorkModeController PRIVATE
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/core/business
)

target_link_libraries(tst_RadarWorkModeController PRIVATE
    Qt5::Core
    Qt5::Test
)

set_target_properties(tst_RadarWorkModeController PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)

add_test(NAME RadarWorkModeController
         COMMAND tst_RadarWorkModeController)

# ----- 协议布局测试 -----
add_executable(tst_ProtocolLayout
    tst_ProtocolLayout.cpp
)

target_include_directories(tst_ProtocolLayout PRIVATE
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/core/protocol
    ${CMAKE_SOURCE_DIR}/core/bus
)

target_link_libraries(tst_ProtocolLayout PRIVATE
    Qt5::Core
    Qt5::Test
)

set_target_properties(tst_ProtocolLayout PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)

add_test(NAME ProtocolLayout
         COMMAND tst_ProtocolLayout)
