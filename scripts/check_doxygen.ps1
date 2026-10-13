<#
/**
 * @file check_doxygen.ps1
 * @brief 审计新架构 C++ 文件是否遵守中文 Doxygen 的最低机械门禁。
 * @details 脚本检查头文件开头的中文 @file、.cpp 函数定义前的中文块注释以及已禁止的模板化句子。
 * 它只提供可自动化发现的下限，不能替代对坐标、时间、所有权、线程和失败语义准确性的人工审查。
 */
#>

param()

$ErrorActionPreference = "Stop"
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$scanRoots = @(
    (Join-Path $repositoryRoot "core"),
    (Join-Path $repositoryRoot "tests"),
    (Join-Path $repositoryRoot "deployment")
)
$failures = [System.Collections.Generic.List[string]]::new()
$headerCount = 0
$cppFunctionCount = 0

foreach ($scanRoot in $scanRoots) {
    if (-not (Test-Path -LiteralPath $scanRoot)) {
        continue
    }

    Get-ChildItem -LiteralPath $scanRoot -Recurse -File | Where-Object {
        @(".h", ".hpp") -contains $_.Extension.ToLowerInvariant()
    } | ForEach-Object {
        $headerCount++
        $relativePath = $_.FullName.Substring($repositoryRoot.Length + 1)
        $head = (Get-Content -LiteralPath $_.FullName -Encoding UTF8 -TotalCount 30) -join "`n"
        if ($head -notmatch '(?m)^[ \t]*\*[ \t]+@file(?:[ \t]|$)') {
            $failures.Add("$relativePath : 前 30 行缺少 @file Doxygen 注释")
        }
        if ($head -notmatch '[\u4e00-\u9fff]') {
            $failures.Add("$relativePath : 文件级 Doxygen 注释必须使用中文")
        }
    }
}

$cppDefinitionPattern = '^[A-Za-z_~][A-Za-z0-9_:<>,*& \t]*\([^;]*\)(\s+const)?(\s+noexcept)?\s*(\{|$)'
foreach ($scanRoot in $scanRoots) {
    if (-not (Test-Path -LiteralPath $scanRoot)) {
        continue
    }

    Get-ChildItem -LiteralPath $scanRoot -Recurse -Filter "*.cpp" -File | ForEach-Object {
        $relativePath = $_.FullName.Substring($repositoryRoot.Length + 1)
        $lines = Get-Content -LiteralPath $_.FullName -Encoding UTF8
        for ($lineIndex = 0; $lineIndex -lt $lines.Count; $lineIndex++) {
            if ($lines[$lineIndex] -notmatch $cppDefinitionPattern) {
                continue
            }

            $cppFunctionCount++
            $commentEnd = $lineIndex - 1
            while ($commentEnd -ge 0 -and [string]::IsNullOrWhiteSpace($lines[$commentEnd])) {
                $commentEnd--
            }
            while ($commentEnd -ge 0 -and $lines[$commentEnd] -match '^\s*template\s*<.*>\s*$') {
                $commentEnd--
                while ($commentEnd -ge 0 -and [string]::IsNullOrWhiteSpace($lines[$commentEnd])) {
                    $commentEnd--
                }
            }
            $commentStart = $commentEnd
            while ($commentStart -ge 0 -and $lines[$commentStart].Trim() -ne "/**") {
                $commentStart--
            }

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

$forbiddenTemplates = @(
    '实现某函数的处理流程',
    '具体契约见头文件',
    '该字段保存业务状态',
    '对应模块的类型与接口'
)
foreach ($scanRoot in $scanRoots) {
    if (-not (Test-Path -LiteralPath $scanRoot)) {
        continue
    }

    Get-ChildItem -LiteralPath $scanRoot -Recurse -File | Where-Object {
        @(".h", ".hpp", ".cpp") -contains $_.Extension.ToLowerInvariant()
    } | ForEach-Object {
        $relativePath = $_.FullName.Substring($repositoryRoot.Length + 1)
        $content = Get-Content -LiteralPath $_.FullName -Encoding UTF8 -Raw
        foreach ($template in $forbiddenTemplates) {
            if ($content.Contains($template)) {
                $failures.Add("$relativePath : 包含无项目知识的模板注释 [$template]")
            }
        }
    }
}

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Error $_ }
    throw "Doxygen 契约检查失败：共发现 $($failures.Count) 个问题。"
}

Write-Host "[OK] $headerCount 个头文件和 $cppFunctionCount 个 C++ 函数通过中文 Doxygen 机械审计。"
