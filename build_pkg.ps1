# Standard UTF8 Header for Windows PowerShell 5.1
$OutputEncoding = [System.Text.Encoding]::UTF8
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$pkgTool = "D:\Projects\Tools\PkgTool\PkgTool.exe"
$gp4Path = Join-Path $scriptDir "ps4_storre.gp4"
$releaseDir = Join-Path $scriptDir "release"

Write-Host "=== Сборка релизного Fake PKG PS4 Storre ===" -ForegroundColor Cyan

if (-not (Test-Path $releaseDir)) {
    New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null
}

if (-not (Test-Path $gp4Path)) {
    throw "Не найден файл проекта $gp4Path"
}

# Проверка ключевых файлов перед сборкой
$required = @(
    "eboot.bin",
    "sce_sys\param.sfo",
    "sce_sys\icon0.png",
    "sce_sys\pic1.png",
    "sce_sys\keystone",
    "sce_sys\about\right.sprx",
    "sce_module\libc.prx",
    "sce_module\libSceFios2.prx",
    "assets\misc\catalog.pak",
    "assets\misc\catalog.json",
    "assets\misc\storre_installerd.elf",
    "assets\misc\storre_torrentd.elf"
)

foreach ($rel in $required) {
    $full = Join-Path $scriptDir $rel
    if (-not (Test-Path $full)) {
        throw "Отсутствует обязательный файл для PKG: $rel ($full)"
    }
}

Write-Host "-> Все обязательные компоненты проверены и готовы." -ForegroundColor Green
Write-Host "-> Запуск PkgTool.exe pkg_build..." -ForegroundColor Cyan

Push-Location $scriptDir
try {
    & $pkgTool pkg_build $gp4Path $releaseDir
    if ($LASTEXITCODE -ne 0) {
        throw "PkgTool pkg_build завершился с кодом ошибки $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}

$expectedPkg = Join-Path $releaseDir "IV0000-STOR00001_00-PS4STORRE0000000.pkg"
if (Test-Path $expectedPkg) {
    $pkgItem = Get-Item $expectedPkg
    $sizeMB = [math]::Round($pkgItem.Length / 1MB, 2)
    Write-Host "=== Релизный PKG успешно собран! ===" -ForegroundColor Green
    Write-Host "Путь: $expectedPkg" -ForegroundColor Yellow
    Write-Host "Размер: $sizeMB МБ ($($pkgItem.Length) байт)" -ForegroundColor Yellow
} else {
    throw "PKG файл не найден в папке релиза: $expectedPkg"
}
