@echo off
setlocal

where g++ >nul 2>&1 || (echo [ERROR] g++ not found, run call env_setup.bat first & pause & exit /b 1)
where cmake >nul 2>&1 || (echo [ERROR] cmake not found, run call env_setup.bat first & pause & exit /b 1)

set BUILD_DIR=build
set CMAKE_GENERATOR=MinGW Makefiles
set QT_PREFIX=C:/Qt/Qt5.14.2/5.14.2/mingw73_64

echo [1/2] Configuring...
cmake -S . -B %BUILD_DIR% -G "%CMAKE_GENERATOR%" -DCMAKE_PREFIX_PATH=%QT_PREFIX% -DCMAKE_BUILD_TYPE=Debug

if errorlevel 1 (
    echo [ERROR] CMake configure failed
    pause
    exit /b 1
)

echo [2/2] Building...
cmake --build %BUILD_DIR% --parallel

if errorlevel 1 (
    echo [ERROR] Build failed
    pause
    exit /b 1
)

echo ========================================
echo  [OK] Build successful!
echo  Binary: %BUILD_DIR%\bin\NewAntiDrone_core.exe
echo ========================================
pause
