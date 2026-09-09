@echo off
setlocal

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0run-debug-tests.ps1" %*
set "TEST_EXIT_CODE=%ERRORLEVEL%"

echo.
if "%TEST_EXIT_CODE%"=="0" (
    echo [Swim Engine] Debug test run completed successfully.
) else (
    echo [Swim Engine] Debug test run FAILED with exit code %TEST_EXIT_CODE%.
)
echo.
pause
exit /b %TEST_EXIT_CODE%
