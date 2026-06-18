@echo off
if "%~1"=="" (
    echo Usage: run_single.bat ^<example_name^>
    exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_single.ps1" %1
exit /b %errorlevel%
