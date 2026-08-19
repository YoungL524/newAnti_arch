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

# ----- TrackPacket protocol-to-core mapping tests -----
add_executable(tst_TrackPacketMapper
    tst_TrackPacketMapper.cpp
    ${CMAKE_SOURCE_DIR}/core/processor/TrackPacketMapper.cpp
    ${CMAKE_SOURCE_DIR}/core/processor/TrackPacketMapper.h
    ${CMAKE_SOURCE_DIR}/core/business/TrackTypes.h
)

target_include_directories(tst_TrackPacketMapper PRIVATE
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/core/processor
    ${CMAKE_SOURCE_DIR}/core/protocol
    ${CMAKE_SOURCE_DIR}/core/business
)

target_link_libraries(tst_TrackPacketMapper PRIVATE
    Qt5::Core
    Qt5::Test
)

set_target_properties(tst_TrackPacketMapper PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)

add_test(NAME TrackPacketMapper
         COMMAND tst_TrackPacketMapper)

# ----- TrackRecognizer 测试 -----
add_executable(tst_TrackRecognizer
    tst_TrackRecognizer.cpp
    ${CMAKE_SOURCE_DIR}/core/processor/TrackRecognizer.cpp
    ${CMAKE_SOURCE_DIR}/core/processor/TrackRecognizer.h
    ${CMAKE_SOURCE_DIR}/core/business/TrackTypes.h
)

target_include_directories(tst_TrackRecognizer PRIVATE
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/core/processor
    ${CMAKE_SOURCE_DIR}/core/business
)

target_link_libraries(tst_TrackRecognizer PRIVATE
    Qt5::Core
    Qt5::Test
)

set_target_properties(tst_TrackRecognizer PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)

add_test(NAME TrackRecognizer
         COMMAND tst_TrackRecognizer)

# ----- Unified DataPacket track publication tests -----
add_executable(tst_TrackDataPacketBus
    tst_TrackDataPacketBus.cpp
    ${CMAKE_SOURCE_DIR}/core/bus/SystemBus.cpp
    ${CMAKE_SOURCE_DIR}/core/bus/SystemBus.h
    ${CMAKE_SOURCE_DIR}/core/bus/ISystemBus.h
    ${CMAKE_SOURCE_DIR}/core/bus/GenericPackets.h
    ${CMAKE_SOURCE_DIR}/core/bus/ThreadSafeQueue.h
    ${CMAKE_SOURCE_DIR}/core/business/TrackTypes.h
)

target_include_directories(tst_TrackDataPacketBus PRIVATE
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/core/protocol
)

target_link_libraries(tst_TrackDataPacketBus PRIVATE
    Qt5::Core
    Qt5::Test
)

set_target_properties(tst_TrackDataPacketBus PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)

add_test(NAME TrackDataPacketBus
         COMMAND tst_TrackDataPacketBus)

# ----- Bounded asynchronous track storage tests -----
add_executable(tst_TrackStorage
    tst_TrackStorage.cpp
    ${CMAKE_SOURCE_DIR}/core/data/TrackStorage.cpp
    ${CMAKE_SOURCE_DIR}/core/data/TrackStorage.h
    ${CMAKE_SOURCE_DIR}/core/data/DataStore.cpp
    ${CMAKE_SOURCE_DIR}/core/data/DataStore.h
    ${CMAKE_SOURCE_DIR}/core/bus/SystemBus.cpp
    ${CMAKE_SOURCE_DIR}/core/bus/SystemBus.h
    ${CMAKE_SOURCE_DIR}/core/bus/ISystemBus.h
    ${CMAKE_SOURCE_DIR}/core/bus/GenericPackets.h
    ${CMAKE_SOURCE_DIR}/core/bus/ThreadSafeQueue.h
    ${CMAKE_SOURCE_DIR}/core/business/TrackTypes.h
)

target_include_directories(tst_TrackStorage PRIVATE
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/core/data
    ${CMAKE_SOURCE_DIR}/core/bus
    ${CMAKE_SOURCE_DIR}/core/business
    ${CMAKE_SOURCE_DIR}/core/protocol
)

target_link_libraries(tst_TrackStorage PRIVATE
    Qt5::Core
    Qt5::Sql
    Qt5::Test
)

set_target_properties(tst_TrackStorage PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)

add_test(NAME TrackStorage COMMAND tst_TrackStorage)

# ----- Initial snapshot + incremental WebSocket track protocol -----
add_executable(tst_TrackDisplayWebDelta
    tst_TrackDisplayWebDelta.cpp
    ${CMAKE_SOURCE_DIR}/core/web/TrackDisplayWebBridge.cpp
    ${CMAKE_SOURCE_DIR}/core/web/TrackDisplayWebBridge.h
    ${CMAKE_SOURCE_DIR}/core/web/WebService.cpp
    ${CMAKE_SOURCE_DIR}/core/web/WebService.h
    ${CMAKE_SOURCE_DIR}/core/web/HttpParser.cpp
    ${CMAKE_SOURCE_DIR}/core/web/HttpParser.h
    ${CMAKE_SOURCE_DIR}/core/bus/SystemBus.cpp
    ${CMAKE_SOURCE_DIR}/core/bus/SystemBus.h
    ${CMAKE_SOURCE_DIR}/core/bus/ISystemBus.h
    ${CMAKE_SOURCE_DIR}/core/bus/GenericPackets.h
    ${CMAKE_SOURCE_DIR}/core/bus/ThreadSafeQueue.h
    ${CMAKE_SOURCE_DIR}/core/business/TrackTypes.h
    ${CMAKE_SOURCE_DIR}/core/metrics/PipelineLatencyMonitor.cpp
    ${CMAKE_SOURCE_DIR}/core/metrics/PipelineLatencyMonitor.h
)

target_include_directories(tst_TrackDisplayWebDelta PRIVATE
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/core/web
    ${CMAKE_SOURCE_DIR}/core/bus
    ${CMAKE_SOURCE_DIR}/core/business
    ${CMAKE_SOURCE_DIR}/core/protocol
    ${CMAKE_SOURCE_DIR}/core/metrics
)

target_link_libraries(tst_TrackDisplayWebDelta PRIVATE
    Qt5::Core
    Qt5::Network
    Qt5::WebSockets
    Qt5::Test
)

set_target_properties(tst_TrackDisplayWebDelta PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)

add_test(NAME TrackDisplayWebDelta COMMAND tst_TrackDisplayWebDelta)

# ----- TrackDisplayManager bounded-window diff tests -----
add_executable(tst_TrackDisplayManagerDelta
    tst_TrackDisplayManagerDelta.cpp
    ${CMAKE_SOURCE_DIR}/core/business/TrackDisplayManager.cpp
    ${CMAKE_SOURCE_DIR}/core/business/TrackDisplayManager.h
    ${CMAKE_SOURCE_DIR}/core/business/TaskService.cpp
    ${CMAKE_SOURCE_DIR}/core/business/TaskService.h
    ${CMAKE_SOURCE_DIR}/core/business/DeviceRegistry.cpp
    ${CMAKE_SOURCE_DIR}/core/business/DeviceRegistry.h
    ${CMAKE_SOURCE_DIR}/core/business/TrackTypes.h
    ${CMAKE_SOURCE_DIR}/core/data/DataStore.cpp
    ${CMAKE_SOURCE_DIR}/core/data/DataStore.h
    ${CMAKE_SOURCE_DIR}/core/bus/SystemBus.cpp
    ${CMAKE_SOURCE_DIR}/core/bus/SystemBus.h
    ${CMAKE_SOURCE_DIR}/core/bus/ISystemBus.h
    ${CMAKE_SOURCE_DIR}/core/bus/GenericPackets.h
    ${CMAKE_SOURCE_DIR}/core/bus/ThreadSafeQueue.h
    ${CMAKE_SOURCE_DIR}/core/metrics/PipelineLatencyMonitor.cpp
    ${CMAKE_SOURCE_DIR}/core/metrics/PipelineLatencyMonitor.h
)

target_include_directories(tst_TrackDisplayManagerDelta PRIVATE
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/core/business
    ${CMAKE_SOURCE_DIR}/core/data
    ${CMAKE_SOURCE_DIR}/core/bus
    ${CMAKE_SOURCE_DIR}/core/protocol
    ${CMAKE_SOURCE_DIR}/core/metrics
)

target_link_libraries(tst_TrackDisplayManagerDelta PRIVATE
    Qt5::Core
    Qt5::Sql
    Qt5::Test
)

set_target_properties(tst_TrackDisplayManagerDelta PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)

add_test(NAME TrackDisplayManagerDelta COMMAND tst_TrackDisplayManagerDelta)
