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

# ----- ConfigManager 测试 -----
add_executable(tst_ConfigManager
    tst_ConfigManager.cpp
    ${CMAKE_SOURCE_DIR}/core/data/ConfigManager.cpp
    ${CMAKE_SOURCE_DIR}/core/data/ConfigManager.h
)

target_include_directories(tst_ConfigManager PRIVATE
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/core/data
)

target_link_libraries(tst_ConfigManager PRIVATE
    Qt5::Core
    Qt5::Test
)

set_target_properties(tst_ConfigManager PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)

add_test(NAME ConfigManager
         COMMAND tst_ConfigManager)

# ----- HttpParser 测试 -----
add_executable(tst_HttpParser
    tst_HttpParser.cpp
    ${CMAKE_SOURCE_DIR}/core/web/HttpParser.cpp
    ${CMAKE_SOURCE_DIR}/core/web/HttpParser.h
)

target_include_directories(tst_HttpParser PRIVATE
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/core/web
)

target_link_libraries(tst_HttpParser PRIVATE
    Qt5::Core
    Qt5::Test
)

set_target_properties(tst_HttpParser PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)

add_test(NAME HttpParser
         COMMAND tst_HttpParser)

# ----- TaskService / DeviceRegistry 领域约束测试 -----
add_executable(tst_TaskService
    tst_TaskService.cpp
    ${CMAKE_SOURCE_DIR}/core/business/TaskService.cpp
    ${CMAKE_SOURCE_DIR}/core/business/TaskService.h
    ${CMAKE_SOURCE_DIR}/core/business/DeviceRegistry.cpp
    ${CMAKE_SOURCE_DIR}/core/business/DeviceRegistry.h
    ${CMAKE_SOURCE_DIR}/core/data/DataStore.cpp
    ${CMAKE_SOURCE_DIR}/core/data/DataStore.h
)

target_include_directories(tst_TaskService PRIVATE
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/core/business
    ${CMAKE_SOURCE_DIR}/core/data
)

target_link_libraries(tst_TaskService PRIVATE
    Qt5::Core
    Qt5::Sql
    Qt5::Test
)

set_target_properties(tst_TaskService PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)

add_test(NAME TaskService
         COMMAND tst_TaskService)

# ----- Pipeline latency aggregation tests -----
add_executable(tst_PipelineLatencyMonitor
    tst_PipelineLatencyMonitor.cpp
    ${CMAKE_SOURCE_DIR}/core/metrics/PipelineLatencyMonitor.cpp
    ${CMAKE_SOURCE_DIR}/core/metrics/PipelineLatencyMonitor.h
)

target_include_directories(tst_PipelineLatencyMonitor PRIVATE
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/core/metrics
)

target_link_libraries(tst_PipelineLatencyMonitor PRIVATE
    Qt5::Core
    Qt5::Test
)

set_target_properties(tst_PipelineLatencyMonitor PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)

add_test(NAME PipelineLatencyMonitor
         COMMAND tst_PipelineLatencyMonitor)
