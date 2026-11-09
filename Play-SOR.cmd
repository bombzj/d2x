@echo off
if "%~1"=="" (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\play.ps1" -Diagnostic -SorceressScenario act1-5
) else (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\play.ps1" -Diagnostic -SorceressScenario %*
)
if errorlevel 1 (
    pause
    exit /b 1
)
