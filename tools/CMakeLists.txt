add_executable(DcmpHeartbeatSimulator
    heartbeat_simulator/main.cpp
)

target_include_directories(DcmpHeartbeatSimulator
    PRIVATE
        ${PROJECT_SOURCE_DIR}
)

target_link_libraries(DcmpHeartbeatSimulator
    PRIVATE
        Qt5::Core
        Qt5::Network
)

set_target_properties(DcmpHeartbeatSimulator PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)
