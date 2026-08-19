set(CORE_HEADERS
    web/HttpParser.h
    web/WebService.h
    web/WebApiController.h
    web/TrackDisplayWebBridge.h
    web/TaskApiRoutes.h
    web/SystemEventApiRoutes.h
    web/TrackHistoryApiRoutes.h
    web/EndpointApiRoutes.h
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
    processor/TrackPacketMapper.h
    processor/TrackRecognizer.h
    processor/BusToCommRouter.h
    processor/DphPublisher.h
    data/ConfigManager.h
    data/DataStore.h
    data/TrackStorage.h
    log/Logger.h
    log/LogContext.h
    metrics/MonotonicClock.h
    metrics/PipelineLatencyMonitor.h
    business/IWorkModeCommandSender.h
    business/RadarWorkModeController.h
    business/BusWorkModeSender.h
    business/DeviceRegistry.h
    business/TaskService.h
    business/RuntimeSettingsTypes.h
    business/RuntimeSettingsService.h
    business/TrackTypes.h
    business/TrackDisplayManager.h
    AppController.h
    StdinReader.h
)

set(CORE_SOURCES
    main.cpp
    web/HttpParser.cpp
    web/WebService.cpp
    web/WebApiController.cpp
    web/TrackDisplayWebBridge.cpp
    web/TaskApiRoutes.cpp
    web/SystemEventApiRoutes.cpp
    web/TrackHistoryApiRoutes.cpp
    web/EndpointApiRoutes.cpp
    log/Logger.cpp
    log/LogContext.cpp
    metrics/PipelineLatencyMonitor.cpp
    comm/Endpoint.cpp
    comm/UdpEndpoint.cpp
    comm/CommunicationManager.cpp
    comm/EndpointConfigLoader.cpp
    bus/SystemBus.cpp
    processor/RadarDataProcessor.cpp
    processor/TrackPacketMapper.cpp
    processor/TrackRecognizer.cpp
    processor/BusToCommRouter.cpp
    processor/DphPublisher.cpp
    data/ConfigManager.cpp
    data/DataStore.cpp
    data/TrackStorage.cpp
    business/RadarWorkModeController.cpp
    business/BusWorkModeSender.cpp
    business/DeviceRegistry.cpp
    business/TaskService.cpp
    business/TrackDisplayManager.cpp
    business/RuntimeSettingsService.cpp
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
    ${CMAKE_CURRENT_SOURCE_DIR}/metrics
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

    # qsqlpsql.dll does not bundle libpq/OpenSSL. Deploy the small runtime set
    # next to the executable so launching from VS Code, Explorer or a clean
    # terminal behaves the same.
    set(POSTGRESQL_BIN_DIR "C:/Program Files/PostgreSQL/17/bin" CACHE PATH
        "PostgreSQL client runtime directory")
    set(POSTGRESQL_RUNTIME_DLLS
        libpq.dll
        libintl-9.dll
        libiconv-2.dll
        libssl-3-x64.dll
        libcrypto-3-x64.dll
    )
    foreach(_pg_dll IN LISTS POSTGRESQL_RUNTIME_DLLS)
        if(EXISTS "${POSTGRESQL_BIN_DIR}/${_pg_dll}")
            add_custom_command(TARGET NewAntiDrone_core POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${POSTGRESQL_BIN_DIR}/${_pg_dll}"
                    "$<TARGET_FILE_DIR:NewAntiDrone_core>/${_pg_dll}"
                COMMENT "Deploying PostgreSQL runtime: ${_pg_dll}"
            )
        endif()
    endforeach()
endif()
