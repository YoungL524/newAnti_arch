cmake_minimum_required(VERSION 3.16)

project(NewAntiDrone
    VERSION 1.0.0
    DESCRIPTION "Anti-drone radar control system (new architecture)"
    LANGUAGES CXX
)

# ---- 全局编译选项 ----
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 让 <cmath> 在 MSVC / MinGW 严格模式下也能拿到 M_PI 等数学常量
add_compile_definitions(_USE_MATH_DEFINES)

# Qt 元对象 / 资源 / UI 自动化
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 构建产物默认输出到 build/bin，避免散落
if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

# 默认 Debug，便于现场调试
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# ---- Qt5 ----
# Windows 下若未把 Qt 加入 CMAKE_PREFIX_PATH，可在配置时传入：
#   cmake -S . -B build -DCMAKE_PREFIX_PATH=C:/Qt/Qt5.14.2/5.14.2/mingw73_64
# Linux 上 apt 安装的 qtbase5-dev 通常无需额外指定。
find_package(Qt5 5.14 REQUIRED COMPONENTS Core Network Sql Widgets WebSockets)

# ---- 单元测试开关 ----
option(BUILD_TESTS "Build unit tests under doc/test/src" ON)
if(BUILD_TESTS)
    enable_testing()
    find_package(Qt5 5.14 REQUIRED COMPONENTS Test)
endif()

# ---- 子项目 ----
add_subdirectory(RSC)
add_subdirectory(qt-material-widgets-master)
add_subdirectory(core)
add_subdirectory(test_comm)
add_subdirectory(demo)

# 让 test_comm 在依赖关系上排在 core 之后
add_dependencies(test_comm NewAntiDrone_core)

if(BUILD_TESTS)
    add_subdirectory(doc/test/src)
endif()

# ---- 配置文件随构建产物一起拷贝（方便直接运行）----
set(_runtime_config_dir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/config")
add_custom_target(copy_runtime_config ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_runtime_config_dir}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/data"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/config/endpoints.json"
            "${_runtime_config_dir}/endpoints.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/config/processor.json"
            "${_runtime_config_dir}/processor.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/config/database.json"
            "${_runtime_config_dir}/database.json"
    COMMENT "Copy runtime config (endpoints/processor/database) to <build>/bin/config/"
)
add_dependencies(NewAntiDrone_core copy_runtime_config)
