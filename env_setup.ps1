$env:QT_DIR = "C:\Qt\Qt5.14.2\5.14.2\mingw73_64"
$env:MINGW_DIR = "C:\Qt\Qt5.14.2\Tools\mingw730_64\bin"
$env:CMAKE_DIR = "C:\Program Files\CMake\bin"

$env:Path = "$env:MINGW_DIR;$env:CMAKE_DIR;$env:QT_DIR\bin;$env:Path"

Write-Host "[OK] MinGW   : $env:MINGW_DIR"
Write-Host "[OK] CMake   : $env:CMAKE_DIR"
Write-Host "[OK] Qt5     : $env:QT_DIR"

if (Get-Command g++ -ErrorAction SilentlyContinue) { Write-Host "[OK] g++ found" }
    else { Write-Host "[WARN] g++ not in PATH" }
if (Get-Command cmake -ErrorAction SilentlyContinue) { Write-Host "[OK] cmake found" }
    else { Write-Host "[WARN] cmake not in PATH" }
if (Get-Command qmake -ErrorAction SilentlyContinue) { Write-Host "[OK] qmake found" }
    else { Write-Host "[WARN] qmake not in PATH" }

Write-Host ""
Write-Host "Running build..."
& cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH=C:/Qt/Qt5.14.2/5.14.2/mingw73_64 -DCMAKE_BUILD_TYPE=Debug
if ($LASTEXITCODE -ne 0) { Write-Host "[ERROR] CMake configure failed"; exit 1 }
& cmake --build build --parallel
if ($LASTEXITCODE -ne 0) { Write-Host "[ERROR] Build failed"; exit 1 }
Write-Host "[OK] Build successful! Binary: build/bin/NewAntiDrone_core.exe"
