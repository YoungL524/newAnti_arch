@echo off
cd /d %~dp0\core

if not exist Makefile (
    qmake core.pro
)

mingw32-make -j8

if errorlevel 1 (
    echo 编译失败
    pause
    exit /b 1
)

echo 编译成功，准备运行...

if not exist debug\endpoints.json (
    copy ..\config\endpoints.json debug\
)

cd debug
NewAntiDrone_core.exe %*
pause
