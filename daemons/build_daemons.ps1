
﻿# Standard UTF8 Header for Windows PowerShell 5.1
$OutputEncoding = [System.Text.Encoding]::UTF8
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$ProjectDir = Split-Path -Parent $ScriptDir
$SdkDir = "D:\Projects\Tools\ps4-payload-sdk"
$BashExe = "C:\Program Files\Git\bin\bash.exe"
$AssetsDaemonsDir = Join-Path $ProjectDir "assets\daemons"

Write-Host "=== Сборка автономных демонов PS4 Storre (storre_installerd + storre_torrentd) ===" -ForegroundColor Cyan

if (-not (Test-Path $SdkDir)) {
    throw "Не найден ps4-payload-sdk: $SdkDir"
}

if (-not (Test-Path $BashExe)) {
    throw "Не найден Git Bash: $BashExe"
}

New-Item -ItemType Directory -Path $AssetsDaemonsDir -Force | Out-Null

# 1. Сборка storre_installerd (порт 8285)
Write-Host "-> Компиляция storre_installerd (порт 8285, /data/storre/installer)..." -ForegroundColor Yellow
$installerDir = "/d/Projects/PS4/ps4_storre/daemons/ps4installer"
& $BashExe -c "export PATH='/d/Projects/Tools/ps4-payload-sdk/bin:/d/Projects/Tools/w64devkit/bin:/usr/bin:`$PATH'; export PS4_PAYLOAD_SDK='/d/Projects/Tools/ps4-payload-sdk'; cd $installerDir && make clean && make"
if ($LASTEXITCODE -ne 0) { throw "Ошибка сборки storre_installerd!" }

# 2. Сборка storre_torrentd (порт 8788)
Write-Host "-> Компиляция storre_torrentd (порт 8788, /data/storre/torrent)..." -ForegroundColor Yellow
$torrentDir = "/d/Projects/PS4/ps4_storre/daemons/ps4torrent"
& $BashExe -c "export PATH='/d/Projects/Tools/ps4-payload-sdk/bin:/d/Projects/Tools/w64devkit/bin:/usr/bin:`$PATH'; export PS4_PAYLOAD_SDK='/d/Projects/Tools/ps4-payload-sdk'; cd $torrentDir && make clean && make"
if ($LASTEXITCODE -ne 0) { throw "Ошибка сборки storre_torrentd!" }

# 3. Копирование в assets/daemons и assets/misc
$AssetsMiscDir = Join-Path $ProjectDir "assets\misc"
New-Item -ItemType Directory -Path $AssetsMiscDir -Force | Out-Null
Copy-Item (Join-Path $ScriptDir "ps4installer\build\storre_installerd.elf") (Join-Path $AssetsDaemonsDir "storre_installerd.elf") -Force
Copy-Item (Join-Path $ScriptDir "ps4torrent\storre_torrentd.elf") (Join-Path $AssetsDaemonsDir "storre_torrentd.elf") -Force
Copy-Item (Join-Path $AssetsDaemonsDir "storre_installerd.elf") (Join-Path $AssetsMiscDir "storre_installerd.elf") -Force
Copy-Item (Join-Path $AssetsDaemonsDir "storre_torrentd.elf") (Join-Path $AssetsMiscDir "storre_torrentd.elf") -Force

Write-Host "=== Все автономные демоны успешно собраны и скопированы в $AssetsDaemonsDir ===" -ForegroundColor Green
Get-ChildItem -Path $AssetsDaemonsDir | Select-Object Name, Length, LastWriteTime | Format-Table -AutoSize
