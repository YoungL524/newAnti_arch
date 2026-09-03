<#
/**
 * @file test_baseline.ps1
 * @brief 使用项目已验证的 Qt/MinGW 工具链执行新架构可重复基线。
 * @details 脚本依次运行中文 Doxygen 审计、CMake 配置、编译和 CTest；只写入调用者指定的仓库内构建
 * 目录，不下载依赖、不修改源码，也不启动雷达设备或网络服务。
 */
#>

param(
    [string]$BuildDir = "build",
    [string]$QtPrefix = "C:\Qt\Qt5.14.2\5.14.2\mingw73_64",
    [string]$MinGwBin = "C:\Qt\Qt5.14.2\Tools\mingw730_64\bin"
)

$ErrorActionPreference = "Stop"
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$cmakePath = (Get-Command cmake -ErrorAction Stop).Source
$ctestPath = Join-Path (Split-Path -Parent $cmakePath) "ctest.exe"
$makePath = Join-Path $MinGwBin "mingw32-make.exe"
$compilerPath = Join-Path $MinGwBin "g++.exe"
$qtBin = Join-Path $QtPrefix "bin"
$auditScript = Join-Path $PSScriptRoot "check_doxygen.ps1"

foreach ($requiredPath in @($ctestPath, $makePath, $compilerPath, $qtBin, $auditScript)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "基线依赖不存在：$requiredPath"
    }
}

if ([System.IO.Path]::IsPathRooted($BuildDir)) {
    $resolvedBuildDir = [System.IO.Path]::GetFullPath($BuildDir)
} else {
    $resolvedBuildDir = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $BuildDir))
}

Write-Host "[1/4] 中文 Doxygen 审计"
& $auditScript
if (-not $?) {
    throw "中文 Doxygen 审计失败。"
}

$originalProcessPath = $env:Path
try {
    $env:Path = "$qtBin;$MinGwBin;$originalProcessPath"

    Write-Host "[2/4] CMake 配置：$resolvedBuildDir"
    & $cmakePath `
        -S $repositoryRoot `
        -B $resolvedBuildDir `
        -G "MinGW Makefiles" `
        -DCMAKE_BUILD_TYPE=Debug `
        "-DCMAKE_PREFIX_PATH=$QtPrefix" `
        "-DCMAKE_MAKE_PROGRAM=$makePath" `
        "-DCMAKE_CXX_COMPILER=$compilerPath" `
        -DBUILD_TESTS=ON
    if ($LASTEXITCODE -ne 0) {
        throw "CMake 配置失败，退出码：$LASTEXITCODE"
    }

    Write-Host "[3/4] 编译"
    & $cmakePath --build $resolvedBuildDir --parallel 8
    if ($LASTEXITCODE -ne 0) {
        throw "编译失败，退出码：$LASTEXITCODE"
    }

    Write-Host "[4/4] CTest"
    & $ctestPath --test-dir $resolvedBuildDir --output-on-failure -C Debug
    if ($LASTEXITCODE -ne 0) {
        throw "CTest 失败，退出码：$LASTEXITCODE"
    }
} finally {
    $env:Path = $originalProcessPath
}

Write-Host "[OK] 新架构基线全部通过。"
