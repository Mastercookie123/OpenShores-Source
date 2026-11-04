@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Hazeron.ps1" FilamentWorldClient
exit /b %errorlevel%
