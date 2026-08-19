param(
    [string]$BuildDir = "build-test",
    [switch]$SkipFrontend
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot ".." )).Path
Set-Location $repo

function Require-Command([string]$Name) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command not found: $Name"
    }
}

Require-Command "cmake"
Require-Command "g++"
Require-Command "qmake"

Write-Host "[1/5] Check mandatory Doxygen contracts"
& (Join-Path $PSScriptRoot "check_doxygen.ps1")

$qtPrefix = $env:QT_PREFIX
if ([string]::IsNullOrWhiteSpace($qtPrefix)) {
    $qtPrefix = "C:/Qt/Qt5.14.2/5.14.2/mingw73_64"
}

$makeProgram = $env:CMAKE_MAKE_PROGRAM
if ([string]::IsNullOrWhiteSpace($makeProgram)) {
    $makeProgram = "C:/Qt/Qt5.14.2/Tools/mingw730_64/bin/mingw32-make.exe"
}

Write-Host "[2/5] Configure C++ test build: $BuildDir"
& cmake -S . -B $BuildDir -G "MinGW Makefiles" `
    -DBUILD_TESTS=ON `
    -DCMAKE_BUILD_TYPE=Debug `
    -DCMAKE_PREFIX_PATH="$qtPrefix" `
    -DCMAKE_MAKE_PROGRAM="$makeProgram"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

Write-Host "[3/5] Build C++ and tests"
& cmake --build $BuildDir --parallel
if ($LASTEXITCODE -ne 0) { throw "C++ build failed" }

Write-Host "[4/5] Run CTest"
& ctest --test-dir $BuildDir --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "CTest failed" }

if (-not $SkipFrontend) {
    Require-Command "npm.cmd"
    Write-Host "[5/5] Build React frontend"
    Push-Location (Join-Path $repo "ui")
    try {
        & npm.cmd run test
        if ($LASTEXITCODE -ne 0) { throw "Frontend tests failed" }
        & npm.cmd run build
        if ($LASTEXITCODE -ne 0) { throw "Frontend build failed" }
    }
    finally {
        Pop-Location
    }
}

Write-Host "[OK] Test baseline passed"
