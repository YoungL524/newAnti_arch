<#
.SYNOPSIS
检查仓库强制执行的中文 Doxygen 契约。
.DESCRIPTION
每个 C++ 头文件和 TypeScript 源文件必须在开头附近包含 @file 注释，
每个 C++ 实现函数前必须包含中文 Doxygen 注释。脚本不依赖 Doxygen 程序本身。
#>

param()

$ErrorActionPreference = "Stop"
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$sourceRoots = @(
    (Join-Path $repositoryRoot "core"),
    (Join-Path $repositoryRoot "test_comm"),
    (Join-Path $repositoryRoot "doc\test\src"),
    (Join-Path $repositoryRoot "ui\src")
)
$extensions = @(".h", ".hpp", ".ts", ".tsx")
$failures = [System.Collections.Generic.List[string]]::new()
$checkedCount = 0
$forbiddenTemplates = @(
    '对应模块的类型、接口与数据契约',
    '本文件属于核心代码',
    '对应的内部处理流程',
    '的启动或创建流程',
    '对应的输入事件或业务步骤',
    '具体输入输出、状态约束和失败语义遵循头文件',
    '该字段保存所属结构定义的业务状态',
    '说明 WebService 的职责与接口契约',
    '读取或计算 LogContext::',
    '只返回当前有效状态',
    '提交前验证目标与数据',
    '按照先阻止新工作',
    '执行必要校验后再改变状态',
    '资源所有权、信号连接和线程亲和性遵循头文件',
    '对应的前端界面、状态或通信能力',
    '采用兼容 Doxygen 的 JSDoc 记录组件职责'
)

foreach ($sourceRoot in $sourceRoots) {
    if (-not (Test-Path -LiteralPath $sourceRoot)) {
        continue
    }

    Get-ChildItem -LiteralPath $sourceRoot -Recurse -File | Where-Object {
        $extensions -contains $_.Extension.ToLowerInvariant()
    } | ForEach-Object {
        $checkedCount++
        $head = (Get-Content -LiteralPath $_.FullName -TotalCount 24) -join "`n"
        if ($head -notmatch '(?m)^[ \t]*\*[ \t]+@file(?:[ \t]|$)') {
            $relativePath = [System.IO.Path]::GetRelativePath($repositoryRoot, $_.FullName)
            $failures.Add("$relativePath : missing an @file Doxygen block in the first 24 lines")
        }
        if ($head -notmatch '[\u4e00-\u9fff]') {
            $relativePath = [System.IO.Path]::GetRelativePath($repositoryRoot, $_.FullName)
            $failures.Add("$relativePath : 文件级 Doxygen 注释必须使用中文")
        }
    }
}

foreach ($configFile in @("ui\vite.config.ts", "ui\tailwind.config.ts")) {
    $fullPath = Join-Path $repositoryRoot $configFile
    if (-not (Test-Path -LiteralPath $fullPath)) {
        continue
    }
    $checkedCount++
    $head = (Get-Content -LiteralPath $fullPath -TotalCount 24) -join "`n"
    if ($head -notmatch '(?m)^[ \t]*\*[ \t]+@file(?:[ \t]|$)') {
        $failures.Add("$configFile : missing an @file Doxygen block in the first 24 lines")
    }
}

$cppDefinitionPattern = '^[A-Za-z_~][A-Za-z0-9_:<>,*& \t]*\([^;]*\)(\s+const)?(\s+noexcept)?\s*(\{|$)'
foreach ($cppRoot in @("core", "test_comm", "doc\test\src")) {
    $fullRoot = Join-Path $repositoryRoot $cppRoot
    if (-not (Test-Path -LiteralPath $fullRoot)) { continue }

    Get-ChildItem -LiteralPath $fullRoot -Recurse -Filter "*.cpp" -File | ForEach-Object {
        $lines = Get-Content -LiteralPath $_.FullName
        for ($lineIndex = 0; $lineIndex -lt $lines.Count; $lineIndex++) {
            if ($lines[$lineIndex] -notmatch $cppDefinitionPattern) { continue }

            $commentEnd = $lineIndex - 1
            while ($commentEnd -ge 0 -and [string]::IsNullOrWhiteSpace($lines[$commentEnd])) { $commentEnd-- }
            $commentStart = $commentEnd
            while ($commentStart -ge 0 -and $lines[$commentStart].Trim() -ne "/**") { $commentStart-- }
            $relativePath = [System.IO.Path]::GetRelativePath($repositoryRoot, $_.FullName)
            if ($commentEnd -lt 0 -or $lines[$commentEnd].Trim() -ne "*/" -or $commentStart -lt 0) {
                $failures.Add("${relativePath}:$($lineIndex + 1) : 函数定义前缺少 Doxygen 注释")
                continue
            }
            $commentText = ($lines[$commentStart..$commentEnd] -join "`n")
            if ($commentText -notmatch '[\u4e00-\u9fff]') {
                $failures.Add("${relativePath}:$($lineIndex + 1) : 函数 Doxygen 注释必须使用中文")
            }
        }
    }
}

foreach ($scanRoot in @("core", "test_comm", "doc\test\src")) {
    $fullRoot = Join-Path $repositoryRoot $scanRoot
    if (-not (Test-Path -LiteralPath $fullRoot)) { continue }
    Get-ChildItem -LiteralPath $fullRoot -Recurse -File | Where-Object {
        @(".h", ".hpp", ".cpp") -contains $_.Extension.ToLowerInvariant()
    } | ForEach-Object {
        $content = Get-Content -LiteralPath $_.FullName -Raw
        $relativePath = [System.IO.Path]::GetRelativePath($repositoryRoot, $_.FullName)
        foreach ($template in $forbiddenTemplates) {
            if ($content.Contains($template)) {
                $failures.Add("${relativePath} : 包含无信息量的模板注释 [$template]")
            }
        }
    }
}

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Error $_ }
    throw "Doxygen 契约检查失败：共发现 $($failures.Count) 个问题。"
}

Write-Host "[OK] $checkedCount 个头文件/前端文件及全部 C++ 函数通过中文 Doxygen 契约检查。"
