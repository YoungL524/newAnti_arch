# Windows 子系统隐藏启动器自身控制台；Widgets 仅提供托盘与启动失败提示。
find_package(Qt5 5.14 REQUIRED COMPONENTS Widgets HINTS ${NEWANTI_QT_SEARCH_HINTS})
add_executable(NewAntiDroneLauncher WIN32
    main.cpp
    DesktopDeployment.cpp
    DesktopDeployment.h
)
target_link_libraries(NewAntiDroneLauncher PRIVATE Qt5::Widgets Qt5::Network)
if(TARGET Qt5::WinMain)
    target_link_libraries(NewAntiDroneLauncher PRIVATE Qt5::WinMain)
endif()
target_compile_definitions(NewAntiDroneLauncher PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0602)
