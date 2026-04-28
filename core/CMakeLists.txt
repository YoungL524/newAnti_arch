set(CORE_HEADERS
    comm/CommTypes.h
    comm/Endpoint.h
    comm/UdpEndpoint.h
    comm/CommunicationManager.h
    comm/EndpointConfigLoader.h
    protocol/StructureSend.h
    protocol/StructureReceive.h
    bus/ThreadSafeQueue.h
    bus/GenericPackets.h
    bus/ISystemBus.h
    bus/SystemBus.h
    processor/RadarDataProcessor.h
    processor/BusToCommRouter.h
    processor/DphPublisher.h
    data/ConfigManager.h
    data/DataStore.h
    AppController.h
    StdinReader.h
)

set(CORE_SOURCES
    main.cpp
    comm/Endpoint.cpp
    comm/UdpEndpoint.cpp
    comm/CommunicationManager.cpp
    comm/EndpointConfigLoader.cpp
    bus/SystemBus.cpp
    processor/RadarDataProcessor.cpp
    processor/BusToCommRouter.cpp
    processor/DphPublisher.cpp
    data/ConfigManager.cpp
    data/DataStore.cpp
    AppController.cpp
)

add_executable(NewAntiDrone_core ${CORE_SOURCES} ${CORE_HEADERS})

target_include_directories(NewAntiDrone_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/comm
    ${CMAKE_CURRENT_SOURCE_DIR}/protocol
    ${CMAKE_CURRENT_SOURCE_DIR}/bus
    ${CMAKE_CURRENT_SOURCE_DIR}/processor
    ${CMAKE_CURRENT_SOURCE_DIR}/data
)

target_link_libraries(NewAntiDrone_core PRIVATE
    Qt5::Core
    Qt5::Network
    Qt5::Sql
)

# 控制台应用（与 .pro 中 CONFIG += console 一致：Windows 下不要 WIN32）
set_target_properties(NewAntiDrone_core PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)
