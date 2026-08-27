@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0validate_core.ps1"
exit /b %errorlevel%
