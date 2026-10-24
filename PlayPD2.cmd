@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\play-pd2.ps1" %*
if errorlevel 1 pause
