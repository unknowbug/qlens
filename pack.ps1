﻿﻿# QLens 打包脚本（Windows Release）
# 用法：powershell -ExecutionPolicy Bypass -File pack.ps1
# 主运行根：build-qv\Release（两 exe + Qt Release + 插件 + language/icons/docs/mcp 已齐）
# 产出：dist\QLens-<ver>\（目录）+ dist\QLens-<ver>.zip
# 2026-08-04: 0.2.1（修复：单击蓝框/双击/框选/多选标签聚合）
$ErrorActionPreference = "Stop"
$root   = $PSScriptRoot
if (-not $root) { $root = (Get-Location).Path }   # 兜底：当前目录
$ver    = "0.2.1"
$qtBin  = "D:\Qt\6.11.1\msvc2022_64\bin"
$src    = Join-Path $root "build-qv\Release"
$dist   = Join-Path $root "dist\QLens-$ver"
$zipOut = Join-Path $root "dist\QLens-$ver.zip"

if (-not (Test-Path (Join-Path $src "qlens_quickview.exe"))) {
    Write-Host "错误：主运行根缺失 $src\qlens_quickview.exe（先构建 QuickView Release + 复制运行集）"
    exit 1
}
if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
New-Item -ItemType Directory -Path $dist -Force | Out-Null
if (Test-Path $zipOut) { Remove-Item $zipOut -Force }

# 1. 从主运行根复制全部（含 Qt Release DLL + plugins + language/icons/docs/mcp + 文档）
Copy-Item "$src\*" $dist -Recurse -Force

# 2. 保险：Qt 运行时（若运行根缺 Qt DLL 则补全；幂等）
& "$qtBin\windeployqt.exe" --no-translations --no-system-d3d-compiler --release "$dist\qlens_manager.exe" 2>$null

# 3. 排除链接副产品（.lib/.exp——非运行需要）
Get-ChildItem $dist -Recurse -Include "*.lib","*.exp" -File | Remove-Item -Force

# 4. 压缩
Compress-Archive -Path "$dist\*" -DestinationPath $zipOut -Force

$n = (Get-ChildItem $dist -Recurse -File | Measure-Object).Count
Write-Host "打包完成：$zipOut（$n 个文件）"
