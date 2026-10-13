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

add_subdirectory(core)

# 单机启动器是独立部署进程，只管理本次启动的进程树，不链接数据域或设备控制实现。
if(WIN32)
    add_subdirectory(deployment)
endif()
