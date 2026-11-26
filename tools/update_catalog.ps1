param(
    [Parameter(Mandatory = $false, Position = 0)]
    [string]$DumpPath,

    [Parameter(Mandatory = $false)]
    [string]$CsvPath,

    [switch]$NoMedia,
    [switch]$CacheOnly,
    [switch]$BuildPkg,
    [switch]$NonInteractive
)

# Standard UTF8 Header for Windows PowerShell 5.1
$OutputEncoding = [System.Text.Encoding]::UTF8
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

# PS4 Storre - Автоматический конвейер обновления базы игр из XML-дампа Rutracker.
# 1. Запуск по правой кнопке мыши («Выполнить с помощью PowerShell») открывает окно выбора файла.
# 2. Извлекает раздачи ветки PS4 (форум 973) из архива rutracker-YYYYMMDD.xml.xz без распаковки на диск.
# 3. Выполняет слияние с локальной базой catalog.json и доупаковку обложек в catalog.pak.
# 4. Синхронизирует обновленный каталог с тестовой сборкой build_win.
# 5. Окно консоли не закрывается автоматически, позволяя прочитать итоговый отчёт.

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$ProjectDir = Split-Path -Parent $ScriptDir
$MasterDir = Join-Path $ScriptDir "master_catalog"
$AssetsMiscDir = Join-Path $ProjectDir "assets\misc"
$BuildWinMiscDir = Join-Path $ProjectDir "build_win\assets\misc"
$ToolsDir = Join-Path $ProjectDir "daemons\ps4installer\tools"
$TempDir = Join-Path $ScriptDir "temp"
$OutputDir = Join-Path $ProjectDir "release"

try {
    # 1. Поиск интерпретатора Python 3
    $PythonExe = "python.exe"
    $KnownPythons = @(
        "C:\Users\Mike\AppData\Local\Programs\Python\Python311\python.exe",
        "C:\Users\$env:USERNAME\AppData\Local\Programs\Python\Python311\python.exe",
        "python.exe"
    )
    foreach ($p in $KnownPythons) {
        if (Test-Path $p) {
            $PythonExe = $p
            break
        }
    }

    Write-Host "=================================================================" -ForegroundColor Cyan
    Write-Host "      PS4 Storre - Мастер обновления базы игр из Rutracker       " -ForegroundColor Cyan
    Write-Host "=================================================================" -ForegroundColor Cyan
    Write-Host "Python: $PythonExe" -ForegroundColor Gray
    Write-Host "Каталог проекта: $ProjectDir" -ForegroundColor Gray
    Write-Host ""

    New-Item -ItemType Directory -Path $TempDir -Force | Out-Null
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

    # 2. Графический диалог выбора файла (если путь не передан в аргументах)
    if (-not $CsvPath -and -not $DumpPath) {
        Write-Host "Открытие проводника для выбора дампа Rutracker..." -ForegroundColor Yellow
        Add-Type -AssemblyName System.Windows.Forms
        $FilePicker = New-Object System.Windows.Forms.OpenFileDialog
        $FilePicker.Title = "PS4 Storre - Выберите дамп Rutracker (.xml.xz) или готовую таблицу (.csv)"
        $FilePicker.Filter = "Выгрузки Rutracker (*.xml.xz;*.csv)|*.xml.xz;*.csv|Архивы Rutracker (*.xml.xz;*.xz)|*.xml.xz;*.xz|Таблицы раздач (*.csv)|*.csv|Все файлы (*.*)|*.*"

        $DownloadsPath = [Environment]::GetFolderPath("UserProfile") + "\Downloads"
        if (Test-Path $DownloadsPath) {
            $FilePicker.InitialDirectory = $DownloadsPath
        }

        # Вывод диалога на передний план
        $TopForm = New-Object System.Windows.Forms.Form
        $TopForm.TopMost = $true
        $DialogResult = $FilePicker.ShowDialog($TopForm)
        $TopForm.Dispose()

        if ($DialogResult -eq [System.Windows.Forms.DialogResult]::OK -and (Test-Path $FilePicker.FileName)) {
            $Chosen = $FilePicker.FileName
            Write-Host "Выбран файл: $Chosen" -ForegroundColor Green
            Write-Host ""
            if ($Chosen.ToLower().EndsWith(".csv")) {
                $CsvPath = $Chosen
            } else {
                $DumpPath = $Chosen
            }
        } else {
            Write-Host "Выбор файла отменен пользователем." -ForegroundColor DarkYellow
            return
        }
    }

    # Валидация выбранных путей
    if ($DumpPath -and -not (Test-Path $DumpPath)) {
        throw "Файл дампа не найден: $DumpPath"
    }

    if ($CsvPath -and -not (Test-Path $CsvPath)) {
        throw "CSV-файл не найден: $CsvPath"
    }

    # 3. ЭТАП 1: Разбор XML-дампа Rutracker (ветка 973)
    $TargetCsv = $CsvPath
    if (-not $TargetCsv) {
        $TargetCsv = Join-Path $TempDir "ps4_games.csv"
        $ParserScript = Join-Path $ToolsDir "rutracker_table.py"

        if (-not (Test-Path $ParserScript)) {
            throw "Не найден скрипт разбора дампа: $ParserScript"
        }

        Write-Host "[1/3] Потоковое извлечение раздач PS4 (форум 973) из дампа..." -ForegroundColor Yellow
        Write-Host "      Архив: $DumpPath" -ForegroundColor Gray
        Write-Host "      Вывод: $TargetCsv" -ForegroundColor Gray
        $sw = [System.Diagnostics.Stopwatch]::StartNew()

        & $PythonExe $ParserScript $DumpPath $TargetCsv
        if ($LASTEXITCODE -ne 0) {
            throw "Сбой при разборе XML-дампа Rutracker (код $LASTEXITCODE)"
        }
        $sw.Stop()
        Write-Host "-> Этап 1 успешно завершен за $($sw.Elapsed.ToString('mm\:ss'))!" -ForegroundColor Green
        Write-Host ""
    } else {
        Write-Host "[1/3] Пропуск разбора дампа (используется готовый CSV: $TargetCsv)" -ForegroundColor Cyan
        Write-Host ""
    }

    # 4. ЭТАП 2: Слияние раздач с полной мастер-базой (tools/master_catalog)
    New-Item -ItemType Directory -Path $MasterDir -Force | Out-Null
    $MasterJson = Join-Path $MasterDir "catalog.json"
    $MasterPak = Join-Path $MasterDir "catalog.pak"
    $CatalogJson = Join-Path $AssetsMiscDir "catalog.json"
    $CatalogPak = Join-Path $AssetsMiscDir "catalog.pak"
    $MergeScript = Join-Path $ToolsDir "merge_torrents.py"
    $PackReleaseScript = Join-Path $ScriptDir "pack_release_catalog.py"

    if (-not (Test-Path $MasterJson) -or -not (Test-Path $MasterPak)) {
        if ((Test-Path $CatalogJson) -and (Test-Path $CatalogPak)) {
            Write-Host "Инициализация мастер-базы в $MasterDir..." -ForegroundColor Cyan
            Copy-Item $CatalogJson $MasterJson -Force
            Copy-Item $CatalogPak $MasterPak -Force
        } else {
            throw "Не найдены исходные файлы мастер-базы в $MasterDir"
        }
    }

    # Резервная копия мастер-каталога перед слиянием
    $BackupJson = Join-Path $MasterDir "catalog.json.bak"
    if (-not (Test-Path $BackupJson)) {
        Write-Host "Создание базовой резервной копии catalog.json.bak..." -ForegroundColor Gray
        Copy-Item $MasterJson $BackupJson -Force
    }

    Write-Host "[2/4] Слияние раздач с мастер-базой (PSN + Rutracker)..." -ForegroundColor Yellow
    Write-Host "      Мастер-база:    $MasterDir" -ForegroundColor Gray
    Write-Host "      Таблица раздач: $TargetCsv" -ForegroundColor Gray

    $MergeArgs = @($MergeScript, $MasterDir, $TargetCsv)
    if ($NoMedia) {
        $MergeArgs += "--no-media"
        Write-Host "      Режим: --no-media (обложки не скачиваются)" -ForegroundColor DarkYellow
    } elseif ($CacheOnly) {
        $MergeArgs += "--cache-only"
        Write-Host "      Режим: --cache-only (картинки только из кэша)" -ForegroundColor DarkYellow
    } else {
        Write-Host "      Режим: Полный (скачивание обложек для новых релизов)" -ForegroundColor Gray
    }

    $swMerge = [System.Diagnostics.Stopwatch]::StartNew()
    & $PythonExe $MergeArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Сбой при слиянии мастер-каталога (код $LASTEXITCODE)"
    }
    $swMerge.Stop()
    Write-Host "-> Этап 2 успешно завершен за $($swMerge.Elapsed.ToString('mm\:ss'))!" -ForegroundColor Green
    Write-Host ""

    # 5. ЭТАП 3: Упаковка облегчённого релизного каталога (только игры с th) и синхронизация
    Write-Host "[3/4] Упаковка облегчённого релизного каталога в assets/misc..." -ForegroundColor Yellow
    & $PythonExe $PackReleaseScript $MasterDir $AssetsMiscDir
    if ($LASTEXITCODE -ne 0) {
        throw "Сбой при упаковке релизного каталога (код $LASTEXITCODE)"
    }
    Write-Host ""

    # 6. ЭТАП 4 (Опционально): Сборка готового PKG-пакета
    if ($BuildPkg) {
        Write-Host "[4/4] Сборка установочного fake PKG пакета..." -ForegroundColor Yellow
        $BuildPkgScript = Join-Path $ProjectDir "build_pkg.ps1"
        if (Test-Path $BuildPkgScript) {
            & $BuildPkgScript
        } else {
            Write-Host "Предупреждение: build_pkg.ps1 не найден. Сборка PKG пропущена." -ForegroundColor Yellow
        }
    }

    Write-Host ""
    Write-Host "=================================================================" -ForegroundColor Green
    Write-Host "     Обновление базы успешно завершено! Каталог актуален.        " -ForegroundColor Green
    Write-Host "=================================================================" -ForegroundColor Green
    Write-Host "Размер catalog.json: $((Get-Item $CatalogJson).Length / 1MB | ForEach-Object { '{0:N2} МБ' -f $_ })" -ForegroundColor Gray
    Write-Host "Размер catalog.pak:  $((Get-Item $CatalogPak).Length / 1MB | ForEach-Object { '{0:N2} МБ' -f $_ })" -ForegroundColor Gray
    Write-Host ""
}
catch {
    Write-Host ""
    Write-Host "ОШИБКА ВЫПОЛНЕНИЯ: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host ""
}
finally {
    if (-not $NonInteractive) {
        Write-Host "Нажмите Enter для закрытия этого окна..." -ForegroundColor Cyan
        try {
            [void][System.Console]::ReadLine()
        } catch {}
    }
}
