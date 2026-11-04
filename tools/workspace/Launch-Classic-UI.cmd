@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Hazeron.ps1" FilamentWorldClient -ClassicUI
exit /b %errorlevel%
