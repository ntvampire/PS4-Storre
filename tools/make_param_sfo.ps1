# Standard UTF8 Header for Windows PowerShell 5.1
$OutputEncoding = [System.Text.Encoding]::UTF8
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$ErrorActionPreference = "Stop"

$env:DOTNET_ROLL_FORWARD = "LatestMajor"
$env:DOTNET_SYSTEM_GLOBALIZATION_INVARIANT = "1"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir
$coreTool = "D:\Projects\Tools\OpenOrbis\bin\windows\PkgTool.Core.exe"
$sfoPath = Join-Path $projectDir "sce_sys\param.sfo"

if (Test-Path $sfoPath) {
    Remove-Item -Path $sfoPath -Force
}

& $coreTool sfo_new $sfoPath
& $coreTool sfo_setentry $sfoPath APP_TYPE --type Integer --maxsize 4 --value 1
& $coreTool sfo_setentry $sfoPath APP_VER --type Utf8 --maxsize 8 --value "01.00"
& $coreTool sfo_setentry $sfoPath ATTRIBUTE --type Integer --maxsize 4 --value 0
& $coreTool sfo_setentry $sfoPath CATEGORY --type Utf8 --maxsize 4 --value "gd"
& $coreTool sfo_setentry $sfoPath CONTENT_ID --type Utf8 --maxsize 48 --value "IV0000-STOR00001_00-PS4STORRE0000000"
& $coreTool sfo_setentry $sfoPath DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
& $coreTool sfo_setentry $sfoPath SYSTEM_VER --type Integer --maxsize 4 --value 0
& $coreTool sfo_setentry $sfoPath TITLE --type Utf8 --maxsize 128 --value "PS4 Storre"
& $coreTool sfo_setentry $sfoPath TITLE_ID --type Utf8 --maxsize 12 --value "STOR00001"
& $coreTool sfo_setentry $sfoPath VERSION --type Utf8 --maxsize 8 --value "01.00"

Write-Host "param.sfo created successfully at $sfoPath" -ForegroundColor Green
Get-Item $sfoPath | Format-Table Name, Length, LastWriteTime
