set(TESTCOMM_HEADERS
    ${CMAKE_SOURCE_DIR}/core/protocol/StructureSend.h
    ${CMAKE_SOURCE_DIR}/core/protocol/StructureReceive.h
    ${CMAKE_SOURCE_DIR}/core/log/Logger.h
    TestComm.h
    StdinReader.h
)

set(TESTCOMM_SOURCES
    main.cpp
    TestComm.cpp
    ${CMAKE_SOURCE_DIR}/core/log/Logger.cpp
)

add_executable(test_comm ${TESTCOMM_SOURCES} ${TESTCOMM_HEADERS})

target_include_directories(test_comm PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/core/protocol
    ${CMAKE_SOURCE_DIR}/core/comm
    ${CMAKE_SOURCE_DIR}/core/log
)

target_link_libraries(test_comm PRIVATE
    Qt5::Core
    Qt5::Network
)

set_target_properties(test_comm PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)
