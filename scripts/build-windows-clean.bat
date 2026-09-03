@echo off
setlocal

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-windows-clean.ps1" %*
set "BUILD_EXIT_CODE=%ERRORLEVEL%"

echo.
if "%BUILD_EXIT_CODE%"=="0" (
    echo [Swim Engine] Windows clean build completed successfully.
) else (
    echo [Swim Engine] Windows clean build FAILED with exit code %BUILD_EXIT_CODE%.
)
echo.
pause
exit /b %BUILD_EXIT_CODE%
