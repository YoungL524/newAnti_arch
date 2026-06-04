set(CORE_HEADERS
    web/HttpParser.h
    web/WebService.h
    web/WebApiController.h
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
    log/Logger.h
    business/IWorkModeCommandSender.h
    business/RadarWorkModeController.h
    business/BusWorkModeSender.h
    AppController.h
    StdinReader.h
)

set(CORE_SOURCES
    main.cpp
    web/HttpParser.cpp
    web/WebService.cpp
    web/WebApiController.cpp
    log/Logger.cpp
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
    business/RadarWorkModeController.cpp
    business/BusWorkModeSender.cpp
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
    ${CMAKE_CURRENT_SOURCE_DIR}/log
    ${CMAKE_CURRENT_SOURCE_DIR}/business
)

target_link_libraries(NewAntiDrone_core PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Network
    Qt5::Sql
    Qt5::WebSockets
)

# 控制台应用（与 .pro 中 CONFIG += console 一致：Windows 下不要 WIN32）
set_target_properties(NewAntiDrone_core PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)

# 每次构建后自动部署 Qt DLL，确保直接双击可运行
if(WIN32)
    find_program(WINDEPLOYQT windeployqt
        HINTS "C:/Qt/Qt5.14.2/5.14.2/mingw73_64/bin"
    )
    if(WINDEPLOYQT)
        add_custom_command(TARGET NewAntiDrone_core POST_BUILD
            COMMAND ${WINDEPLOYQT} $<TARGET_FILE:NewAntiDrone_core>
            COMMENT "Deploying Qt DLLs via windeployqt..."
        )
    endif()
endif()
