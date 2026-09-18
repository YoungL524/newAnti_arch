cmake_minimum_required(VERSION 3.16)

project(NewAntiDrone
    VERSION 2.0.0
    DESCRIPTION "反无人机雷达上位机共享强类型数据域"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "构建类型" FORCE)
endif()

# 将命令行传入的标准 CMAKE_PREFIX_PATH 显式作为 Qt Config 搜索提示使用。
# 这既支持全新构建目录，也避免已有 Qt5_DIR 缓存时被 CMake 误报为“未使用变量”。
set(NEWANTI_QT_SEARCH_HINTS ${CMAKE_PREFIX_PATH})
find_package(Qt5 5.14 REQUIRED COMPONENTS Core Network Sql WebSockets HINTS ${NEWANTI_QT_SEARCH_HINTS})

option(BUILD_TESTS "构建新架构自动化测试" ON)
if(BUILD_TESTS)
    enable_testing()
    find_package(Qt5 5.14 REQUIRED COMPONENTS Test HINTS ${NEWANTI_QT_SEARCH_HINTS})
endif()

add_subdirectory(core)

option(BUILD_TOOLS "构建协议联试工具" ON)
if(BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
