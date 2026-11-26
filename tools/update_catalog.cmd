@echo off
chcp 65001 >nul
title PS4 Storre - Обновление базы игр из Rutracker
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0update_catalog.ps1"
