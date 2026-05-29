@echo off
REM ============================================================
REM  NewAntiDrone build environment setup
REM  Usage: call env_setup.bat
REM ============================================================

set QT_DIR=C:\Qt\Qt5.14.2\5.14.2\mingw73_64
set MINGW_DIR=C:\Qt\Qt5.14.2\Tools\mingw730_64\bin
set CMAKE_DIR=C:\Program Files\CMake\bin

set PATH=%MINGW_DIR%;%CMAKE_DIR%;%QT_DIR%\bin;%PATH%

echo [OK] MinGW   : %MINGW_DIR%
echo [OK] CMake   : %CMAKE_DIR%
echo [OK] Qt5     : %QT_DIR%

where g++ >nul 2>&1 && (echo [OK] g++ found) || (echo [WARN] g++ not in PATH)
where cmake >nul 2>&1 && (echo [OK] cmake found) || (echo [WARN] cmake not in PATH)
where qmake >nul 2>&1 && (echo [OK] qmake found) || (echo [WARN] qmake not in PATH)
