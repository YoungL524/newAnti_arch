<#
/**
 * @file package_single_machine.ps1
 * @brief 将用户已编译的单机部署产物收集成 dcmp/dp/sp 同级目录。
 * @details 输入是 Core/Launcher EXE、UI dist、Qt 工具和固定 Node 运行时；输出为全新部署目录。
 * 脚本不构建、不运行测试、不启动业务程序，不覆盖现存包或数据库；阅读入口为下方参数与前置检查。
 */
#>
param(
    [Parameter(Mandatory = $true)][string]$BuildDirectory,
    [Parameter(Mandatory = $true)][string]$QtBinDirectory,
    [string]$NodeExecutable = (Get-Command node.exe -ErrorAction Stop).Source,
    [string]$CompilerBinDirectory = '',
    [string]$MapFile = (Join-Path $PSScriptRoot '..\ui\maps\basemap.mbtiles'),
    [string]$XyzTileDirectory = '',
    [string]$DestinationRoot = (Join-Path $PSScriptRoot '..\release'),
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildRoot = (Resolve-Path -LiteralPath $BuildDirectory).Path
$qtRoot = (Resolve-Path -LiteralPath $QtBinDirectory).Path
$nodePath = (Resolve-Path -LiteralPath $NodeExecutable).Path
$mapPath = if ($MapFile) { (Resolve-Path -LiteralPath $MapFile).Path } else { '' }
<# /** @brief XYZ 是可选静态目录；未提供时只在源码默认目录存在时收集，不生成或下载瓦片。 */ #>
$xyzRoot = ''
if ($XyzTileDirectory) { $xyzRoot = (Resolve-Path -LiteralPath $XyzTileDirectory).Path }
elseif (Test-Path -LiteralPath (Join-Path $repositoryRoot 'ui\maps\xyz') -PathType Container) {
    $xyzRoot = (Resolve-Path -LiteralPath (Join-Path $repositoryRoot 'ui\maps\xyz')).Path
}
if ($xyzRoot -and -not (Test-Path -LiteralPath $xyzRoot -PathType Container)) { throw 'XYZ 输入必须是目录' }
if (-not $mapPath -and -not $xyzRoot) { throw '至少提供 MBTiles 或 XYZ 目录中的一种离线地图' }
$destination = [System.IO.Path]::GetFullPath($DestinationRoot)
$windeploy = Join-Path $qtRoot 'windeployqt.exe'
$qmake = Join-Path $qtRoot 'qmake.exe'

<#
/** @brief 读取 PE machine 字段，防止将 x86 Qt、Core 或 Node 与 x64 原生 PTY 混装；不执行 EXE。 */
#>
function Get-PeMachine([string]$Executable) {
    $stream = [System.IO.File]::OpenRead($Executable)
    $reader = [System.IO.BinaryReader]::new($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5a4d) { throw "非 PE 文件：$Executable" }
        $stream.Position = 0x3c
        $offset = $reader.ReadInt32()
        if ($offset -lt 0 -or $offset + 6 -gt $stream.Length) { throw "PE 头无效：$Executable" }
        $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550) { throw "PE 签名无效：$Executable" }
        return $reader.ReadUInt16()
    } finally { $reader.Dispose() }
}

<#
/** @brief 兼容单配置和多配置构建目录；找不到用户已经编译的 EXE 时直接失败，不触发构建。 */
#>
function Find-BuildExecutable([string]$RelativeDirectory, [string]$Name) {
    foreach ($candidate in @(
        (Join-Path $buildRoot "$RelativeDirectory\$Name"),
        (Join-Path $buildRoot "$RelativeDirectory\$Configuration\$Name")
    )) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    }
    throw "缺少编译产物：$RelativeDirectory\$Name；请先完成 $Configuration 构建。"
}

$core = Find-BuildExecutable 'core' 'NewAntiDrone.exe'
$launcher = Find-BuildExecutable 'deployment' 'NewAntiDroneLauncher.exe'
$dist = Join-Path $repositoryRoot 'ui\dist'
$modules = Join-Path $repositoryRoot 'ui\node_modules'
foreach ($required in @($windeploy, $qmake, (Join-Path $dist 'index.html'),
    (Join-Path $modules 'node-pty\prebuilds\win32-x64\conpty.node'),
    (Join-Path $modules 'node-pty\prebuilds\win32-x64\conpty_console_list.node'))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "缺少部署输入：$required" }
}
<#
/**
 * @brief 拒绝明显早于源码修改的产物，防止将已修复数据库代码与旧 EXE 混同；只检查时间，不触发构建。
 * @param Artifact 用户生成的 EXE 或 UI 首页；其修改时间作为构建水位。
 * @param SourceDirectory 对应源码目录；调用前必须存在。
 * @param Extensions 参与水位比较的扩展名，避免把文档和缓存当成源码。
 * @note 时间戳只能发现明显旧产物，不能替代构建成功或运行验收。
 */
#>
function Assert-ArtifactFresh([string]$Artifact, [string]$SourceDirectory, [string[]]$Extensions) {
    $artifactTime = (Get-Item -LiteralPath $Artifact).LastWriteTimeUtc
    $newerSource = Get-ChildItem -LiteralPath $SourceDirectory -File -Recurse |
        Where-Object { $_.Extension -in $Extensions -and $_.LastWriteTimeUtc -gt $artifactTime } |
        Select-Object -First 1
    if ($newerSource) { throw "产物早于源码修改，请重新构建后打包：$Artifact；较新的输入：$($newerSource.FullName)" }
}
Assert-ArtifactFresh $core (Join-Path $repositoryRoot 'core') @('.cpp', '.h')
Assert-ArtifactFresh $launcher (Join-Path $repositoryRoot 'deployment') @('.cpp', '.h')
Assert-ArtifactFresh (Join-Path $dist 'index.html') (Join-Path $repositoryRoot 'ui\src') @('.ts', '.tsx', '.css', '.ttf', '.woff', '.woff2')
foreach ($executable in @($core, $launcher, $nodePath, (Join-Path $qtRoot 'Qt5Core.dll'))) {
    if ((Get-PeMachine $executable) -ne 0x8664) { throw "第一版部署包要求全部为 Windows x64：$executable" }
}
<# /** @brief 地图读取使用随包 Node 的内置 SQLite；要求 Node 24 及以上，避免现场缺少原生 SQLite 模块。 */ #>
$nodeVersion = (Get-Item -LiteralPath $nodePath).VersionInfo.ProductVersion
if ($nodeVersion -notmatch '^v?(\d+)\.' -or [int]$Matches[1] -lt 24) {
    throw "离线地图部署要求 Node 24 或以上：$nodeVersion"
}
foreach ($package in @('ws', 'node-pty', 'node-addon-api')) {
    if (-not (Test-Path -LiteralPath (Join-Path $modules "$package\package.json") -PathType Leaf)) { throw "缺少运行依赖：$package" }
}
<# /** @brief 从指定 Qt 查询工具链，避免 PATH 中其他 Qt 或目录名称影响插件筛选规则。 */ #>
$qtSpec = & $qmake -query QMAKE_XSPEC
if ($LASTEXITCODE -ne 0) { throw '无法读取指定 Qt 的工具链配置' }
$isMinGW = ([string]$qtSpec).Trim() -match '^win32-(clang-)?g\+\+$'
if (-not $CompilerBinDirectory -and $isMinGW) {
    $cache = Join-Path $buildRoot 'CMakeCache.txt'
    if (Test-Path -LiteralPath $cache -PathType Leaf) {
        foreach ($line in Get-Content -LiteralPath $cache) {
            if ($line -match '^CMAKE_CXX_COMPILER:(?:FILEPATH|STRING)=(.+)$') {
                $CompilerBinDirectory = Split-Path -Parent $Matches[1]
                break
            }
        }
    }
}
if ($CompilerBinDirectory) { $CompilerBinDirectory = (Resolve-Path -LiteralPath $CompilerBinDirectory).Path }
if (Test-Path -LiteralPath $destination) {
    if (-not (Test-Path -LiteralPath $destination -PathType Container) -or @(Get-ChildItem -LiteralPath $destination -Force).Count -ne 0) {
        throw "输出目录必须全新或为空，不覆盖既有部署：$destination"
    }
}
$dcmp = Join-Path $destination 'dcmp'
$coreTarget = Join-Path $dcmp 'runtime\core'
$webTarget = Join-Path $dcmp 'runtime\web'
foreach ($directory in @($dcmp, $coreTarget, "$webTarget\server", "$webTarget\terminal", "$webTarget\node_modules", "$webTarget\maps\xyz",
    (Join-Path $destination 'dp'), (Join-Path $destination 'sp'))) {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}
Copy-Item -LiteralPath $launcher -Destination (Join-Path $dcmp 'NewAntiDrone.exe')
Copy-Item -LiteralPath $core -Destination (Join-Path $coreTarget 'NewAntiDroneCore.exe')
Copy-Item -LiteralPath $nodePath -Destination (Join-Path $dcmp 'runtime\node.exe')
Copy-Item -LiteralPath (Join-Path $repositoryRoot 'deployment\deployment.json') -Destination $dcmp
Copy-Item -LiteralPath (Join-Path $repositoryRoot 'ui\server\production.mjs') -Destination "$webTarget\server"
Copy-Item -LiteralPath (Join-Path $repositoryRoot 'ui\server\offline-map.mjs') -Destination "$webTarget\server"
if ($mapPath) { Copy-Item -LiteralPath $mapPath -Destination (Join-Path $webTarget 'maps\basemap.mbtiles') }
if ($xyzRoot) { Get-ChildItem -LiteralPath $xyzRoot -Force | Copy-Item -Destination (Join-Path $webTarget 'maps\xyz') -Recurse }
Copy-Item -LiteralPath (Join-Path $repositoryRoot 'ui\terminal\server.mjs') -Destination "$webTarget\terminal"
Copy-Item -LiteralPath $dist -Destination $webTarget -Recurse
# 当前生产入口只有 ws、node-pty 及其 node-addon-api 依赖；不打包 Vite、TypeScript 或 React 源码。
foreach ($package in @('ws', 'node-pty', 'node-addon-api')) {
    Copy-Item -LiteralPath (Join-Path $modules $package) -Destination "$webTarget\node_modules" -Recurse
}
$originalPath = $env:PATH
try {
    $env:PATH = "$qtRoot;$CompilerBinDirectory;$originalPath"
    <# /** @brief Qt 5.14.2 无法可靠区分 MinGW 插件的调试标记，强制 --release 会误筛掉 qwindows.dll。
     * MinGW 保留自动识别；MSVC 仍按用户配置区分两套运行库。 */ #>
    $deployArguments = @('--compiler-runtime', '--no-translations')
    if (-not $isMinGW) {
        $deployArguments += $(if ($Configuration -eq 'Debug') { '--debug' } else { '--release' })
    }
    & $windeploy @deployArguments (Join-Path $dcmp 'NewAntiDrone.exe')
    if ($LASTEXITCODE -ne 0) { throw '启动器 Qt 依赖收集失败' }
    & $windeploy @deployArguments (Join-Path $coreTarget 'NewAntiDroneCore.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Core Qt 依赖收集失败' }
} finally { $env:PATH = $originalPath }
if (-not (Test-Path -LiteralPath (Join-Path $dcmp 'platforms\qwindows.dll') -PathType Leaf) -and
    -not (Test-Path -LiteralPath (Join-Path $dcmp 'platforms\qwindowsd.dll') -PathType Leaf)) {
    throw '部署包缺少 Windows Qt 平台插件；当前输出仍是未完成包，不交付用户。'
}
if (-not (Test-Path -LiteralPath (Join-Path $coreTarget 'sqldrivers\qsqlite.dll') -PathType Leaf) -and
    -not (Test-Path -LiteralPath (Join-Path $coreTarget 'sqldrivers\qsqlited.dll') -PathType Leaf)) {
    throw '部署包缺少 SQLite Qt 插件；当前输出仍是未完成包，不交付用户。'
}
Copy-Item -LiteralPath (Join-Path $repositoryRoot 'doc\contracts\单机部署契约.md') -Destination (Join-Path $destination '部署说明.md')
<# /** @brief 为已收集的不可变程序和网页记录 SHA256，现场可比对实际版本；不包含配置、数据库或 DP/SP 用户文件。 */ #>
$manifestFiles = @(
    (Get-Item -LiteralPath (Join-Path $dcmp 'NewAntiDrone.exe')),
    (Get-Item -LiteralPath (Join-Path $coreTarget 'NewAntiDroneCore.exe')),
    (Get-Item -LiteralPath (Join-Path $dcmp 'runtime\node.exe'))
) + @(Get-ChildItem -LiteralPath (Join-Path $webTarget 'dist') -File -Recurse) + @(
    (Get-Item -LiteralPath (Join-Path $webTarget 'server\production.mjs')),
    (Get-Item -LiteralPath (Join-Path $webTarget 'server\offline-map.mjs')),
    (Get-Item -LiteralPath (Join-Path $webTarget 'terminal\server.mjs'))
)
$manifestFiles += @(Get-ChildItem -LiteralPath (Join-Path $webTarget 'maps') -File -Recurse)
$manifest = [ordered]@{
    packagedUtc = [DateTime]::UtcNow.ToString('o')
    configuration = $Configuration
    files = @($manifestFiles | ForEach-Object {
        [ordered]@{
            path = [IO.Path]::GetRelativePath($destination, $_.FullName).Replace('\', '/')
            sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
            sourceModifiedUtc = $_.LastWriteTimeUtc.ToString('o')
        }
    })
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $destination 'package-manifest.json') -Encoding utf8
Write-Host "已收集部署包：$destination"
Write-Host '用户入口：dcmp\NewAntiDrone.exe；DP/SP 默认禁用。尚需干净 Windows 电脑验收。'
