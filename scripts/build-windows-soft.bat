@echo off
setlocal

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-windows-soft.ps1" %*
set "BUILD_EXIT_CODE=%ERRORLEVEL%"

echo.
if "%BUILD_EXIT_CODE%"=="0" (
    echo [Swim Engine] Windows soft build completed successfully.
) else (
    echo [Swim Engine] Windows soft build FAILED with exit code %BUILD_EXIT_CODE%.
)
echo.
pause
exit /b %BUILD_EXIT_CODE%
