﻿# Standard UTF8 Header for Windows PowerShell 5.1
$OutputEncoding = [System.Text.Encoding]::UTF8
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$openOrbis = "D:\Projects\Tools\OpenOrbis"
$llvmBin   = "D:\Projects\Tools\LLVM\bin"
$fselfTool = Join-Path $openOrbis "bin\windows\create-fself.exe"
$buildDir  = Join-Path $scriptDir "build_ps4"

Write-Host "=== Сборка PS4 Storre eboot.bin (OpenOrbis / LLVM) ===" -ForegroundColor Cyan

if (-not (Test-Path $buildDir)) {
    New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
}

$cxxFlags = @(
    "--target=x86_64-pc-freebsd12-elf",
    "-fPIC",
    "-funwind-tables",
    "-O2",
    "-DPS4=1",
    "-I$openOrbis\include",
    "-I$openOrbis\include\SDL2",
    "-I$openOrbis\include\c++\v1",
    "-I$scriptDir\include",
    "-I$scriptDir\src"
)

$cFlags = @(
    "--target=x86_64-pc-freebsd12-elf",
    "-fPIC",
    "-funwind-tables",
    "-O2",
    "-I$openOrbis\include",
    "-I$scriptDir\include"
)

$sourceFiles = @(
    @{ Path = "$scriptDir\src\main.cpp";                 Out = "$buildDir\main.o";         IsCxx = $true },
    @{ Path = "$scriptDir\src\catalog\catalog.cpp";       Out = "$buildDir\catalog.o";      IsCxx = $true },
    @{ Path = "$scriptDir\src\network\http_client.cpp";   Out = "$buildDir\http_client.o";  IsCxx = $true },
    @{ Path = "$scriptDir\src\network\orchestrator.cpp";  Out = "$buildDir\orchestrator.o"; IsCxx = $true },
    @{ Path = "$scriptDir\src\ps4_ttf.cpp";              Out = "$buildDir\ps4_ttf.o";      IsCxx = $true },
    @{ Path = "$scriptDir\include\cJSON.c";              Out = "$buildDir\cJSON.o";        IsCxx = $false }
)

$clang   = Join-Path $llvmBin "clang.exe"
$clangxx = Join-Path $llvmBin "clang++.exe"
$lld     = Join-Path $llvmBin "ld.lld.exe"

$objFiles = @()

foreach ($src in $sourceFiles) {
    $srcPath = $src.Path
    $outPath = $src.Out
    $objFiles += $outPath
    $fileName = Split-Path -Leaf $srcPath

    Write-Host "-> Компиляция $fileName..." -ForegroundColor Gray
    if ($src.IsCxx) {
        & $clangxx @cxxFlags -c $srcPath -o $outPath
    } else {
        & $clang @cFlags -c $srcPath -o $outPath
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Ошибка компиляции $fileName"
    }
}

$outputElf = Join-Path $buildDir "ps4_storre.elf"
$outputOelf = Join-Path $buildDir "ps4_storre.oelf"
$ebootBin = Join-Path $scriptDir "eboot.bin"

Write-Host "-> Линковка ps4_storre.elf..." -ForegroundColor Cyan

$linkArgs = @(
    "-m", "elf_x86_64",
    "-pie",
    "--script", "$openOrbis\link.x",
    "--eh-frame-hdr",
    "-o", $outputElf,
    "-L$openOrbis\lib",
    "-lc",
    "-lkernel",
    "-lc++",
    "-lSceUserService",
    "-lSceVideoOut",
    "-lSceAudioOut",
    "-lScePad",
    "-lSceSysmodule",
    "-lSceFreeType",
    "-lSceNet",
    "-lSDL2",
    "-lSDL2_image",
    "$openOrbis\lib\crt1.o"
) + $objFiles

& $lld @linkArgs
if ($LASTEXITCODE -ne 0) {
    throw "Ошибка линковки ps4_storre.elf"
}

Write-Host "-> Создание подписанного eboot.bin (FSELF)..." -ForegroundColor Cyan
& $fselfTool -in $outputElf --out $outputOelf --eboot "eboot.bin" --paid 0x3800000000000011
if ($LASTEXITCODE -ne 0) {
    throw "Ошибка create-fself"
}

Move-Item -Path "eboot.bin" -Destination $ebootBin -Force
Write-Host "=== Сборка eboot.bin успешно завершена: $ebootBin ($((Get-Item $ebootBin).Length) байт) ===" -ForegroundColor Green
