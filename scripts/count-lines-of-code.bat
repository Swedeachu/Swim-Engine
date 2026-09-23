@echo off
setlocal

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0count-lines-of-code.ps1" %*
set "COUNT_EXIT_CODE=%ERRORLEVEL%"

echo.
if not "%COUNT_EXIT_CODE%"=="0" (
    echo [Swim Engine] Line count FAILED with exit code %COUNT_EXIT_CODE%.
)
pause
exit /b %COUNT_EXIT_CODE%
