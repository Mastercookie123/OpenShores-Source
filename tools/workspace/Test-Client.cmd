@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Test-Client.ps1" %*
exit /b %errorlevel%
