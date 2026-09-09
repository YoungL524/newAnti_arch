add_executable(tst_HistoryTopic
    tst_HistoryTopic.cpp
)

target_link_libraries(tst_HistoryTopic
    PRIVATE
        NewAntiDrone_datadomain
        Qt5::Test
)

set_target_properties(tst_HistoryTopic PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)

add_test(NAME datadomain.HistoryTopic COMMAND $<TARGET_FILE:tst_HistoryTopic>)

add_executable(tst_StateAndEventTopic
    tst_StateAndEventTopic.cpp
)

target_link_libraries(tst_StateAndEventTopic
    PRIVATE
        NewAntiDrone_datadomain
        Qt5::Test
)

set_target_properties(tst_StateAndEventTopic PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)

add_test(NAME datadomain.StateAndEventTopic COMMAND $<TARGET_FILE:tst_StateAndEventTopic>)

add_executable(tst_TrackModelAndDomain
    tst_TrackModelAndDomain.cpp
)

target_link_libraries(tst_TrackModelAndDomain
    PRIVATE
        NewAntiDrone_datadomain
        Qt5::Test
)

set_target_properties(tst_TrackModelAndDomain PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)

add_test(NAME datadomain.TrackModelAndDomain COMMAND $<TARGET_FILE:tst_TrackModelAndDomain>)

add_executable(tst_TrackPublisher
    tst_TrackPublisher.cpp
)

target_link_libraries(tst_TrackPublisher
    PRIVATE
        NewAntiDrone_datadomain
        Qt5::Test
)

set_target_properties(tst_TrackPublisher PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)

add_test(NAME producer.TrackPublisher COMMAND $<TARGET_FILE:tst_TrackPublisher>)

add_executable(tst_EntityIds
    tst_EntityIds.cpp
)

target_link_libraries(tst_EntityIds
    PRIVATE
        NewAntiDrone_datadomain
        Qt5::Test
)

set_target_properties(tst_EntityIds PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)

add_test(NAME model.EntityIds COMMAND $<TARGET_FILE:tst_EntityIds>)

add_executable(tst_SystemEventPublisher
    tst_SystemEventPublisher.cpp
)

target_link_libraries(tst_SystemEventPublisher
    PRIVATE
        NewAntiDrone_datadomain
        Qt5::Test
)

set_target_properties(tst_SystemEventPublisher PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)

add_test(NAME producer.SystemEventPublisher COMMAND $<TARGET_FILE:tst_SystemEventPublisher>)

add_executable(tst_ExecutionLane
    tst_ExecutionLane.cpp
)

target_link_libraries(tst_ExecutionLane
    PRIVATE
        NewAntiDrone_datadomain
        Qt5::Test
)

set_target_properties(tst_ExecutionLane PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)

add_test(NAME execution.ExecutionLane COMMAND $<TARGET_FILE:tst_ExecutionLane>)

add_executable(tst_RealtimeDataPathStress
    tst_RealtimeDataPathStress.cpp
)

target_link_libraries(tst_RealtimeDataPathStress
    PRIVATE
        NewAntiDrone_datadomain
        Qt5::Test
)

set_target_properties(tst_RealtimeDataPathStress PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)

add_test(NAME stress.RealtimeDataPath COMMAND $<TARGET_FILE:tst_RealtimeDataPathStress>)

add_executable(tst_ArchiveReader
    tst_ArchiveReader.cpp
)

target_link_libraries(tst_ArchiveReader
    PRIVATE
        NewAntiDrone_datadomain
        Qt5::Test
)

set_target_properties(tst_ArchiveReader PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)

add_test(NAME archive.ArchiveReader COMMAND $<TARGET_FILE:tst_ArchiveReader>)

add_executable(tst_SqliteArchiveRepository
    tst_SqliteArchiveRepository.cpp
)

target_link_libraries(tst_SqliteArchiveRepository
    PRIVATE
        NewAntiDrone_datadomain
        Qt5::Test
)

set_target_properties(tst_SqliteArchiveRepository PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)

add_test(NAME archive.SqliteArchiveRepository COMMAND $<TARGET_FILE:tst_SqliteArchiveRepository>)

add_executable(tst_ConfigAndTaskServices
    tst_ConfigAndTaskServices.cpp
)

target_link_libraries(tst_ConfigAndTaskServices
    PRIVATE
        NewAntiDrone_datadomain
        Qt5::Test
)

set_target_properties(tst_ConfigAndTaskServices PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)

add_test(NAME services.ConfigAndTask COMMAND $<TARGET_FILE:tst_ConfigAndTaskServices>)

add_executable(tst_TrackWebSocketService
    tst_TrackWebSocketService.cpp
)

target_link_libraries(tst_TrackWebSocketService
    PRIVATE
        NewAntiDrone_datadomain
        Qt5::Test
)

set_target_properties(tst_TrackWebSocketService PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)

add_test(NAME web.TrackWebSocketService COMMAND $<TARGET_FILE:tst_TrackWebSocketService>)

add_executable(tst_RuntimeStateGate
    tst_RuntimeStateGate.cpp
)

target_link_libraries(tst_RuntimeStateGate
    PRIVATE
        NewAntiDrone_datadomain
        Qt5::Test
)

set_target_properties(tst_RuntimeStateGate PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)

add_test(NAME state.RuntimeStateGate COMMAND $<TARGET_FILE:tst_RuntimeStateGate>)

add_executable(tst_DcmpProtocolTypes
    tst_DcmpProtocolTypes.cpp
)

target_link_libraries(tst_DcmpProtocolTypes
    PRIVATE
        NewAntiDrone_datadomain
        Qt5::Test
)

set_target_properties(tst_DcmpProtocolTypes PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)

add_test(NAME protocol.DcmpProtocolTypes COMMAND $<TARGET_FILE:tst_DcmpProtocolTypes>)

add_executable(tst_UdpEndpoint tst_UdpEndpoint.cpp)
target_link_libraries(tst_UdpEndpoint PRIVATE NewAntiDrone_datadomain Qt5::Test)
add_test(NAME comm.UdpEndpoint COMMAND $<TARGET_FILE:tst_UdpEndpoint>)

add_executable(tst_DcmpProtocolAdapter tst_DcmpProtocolAdapter.cpp)
target_link_libraries(tst_DcmpProtocolAdapter PRIVATE NewAntiDrone_datadomain Qt5::Test)
add_test(NAME protocol.DcmpProtocolAdapter COMMAND $<TARGET_FILE:tst_DcmpProtocolAdapter>)
add_executable(tst_HeartbeatPath tst_HeartbeatPath.cpp)
target_link_libraries(tst_HeartbeatPath PRIVATE NewAntiDrone_datadomain Qt5::Test)
add_test(NAME integration.HeartbeatPath COMMAND $<TARGET_FILE:tst_HeartbeatPath>)
