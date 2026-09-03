@echo off
setlocal

where wsl.exe >nul 2>nul
if errorlevel 1 (
    echo [Swim Engine] Linux clean build requires Windows Subsystem for Linux ^(WSL^).
    set "BUILD_EXIT_CODE=1"
    goto :done
)

for /f "usebackq delims=" %%I in (`wsl.exe wslpath -a "%~dp0build-linux-clean.sh"`) do set "WSL_SCRIPT=%%I"
if not defined WSL_SCRIPT (
    echo [Swim Engine] Failed to resolve the Linux clean-build script path through WSL.
    set "BUILD_EXIT_CODE=1"
    goto :done
)

wsl.exe bash "%WSL_SCRIPT%" %*
set "BUILD_EXIT_CODE=%ERRORLEVEL%"

:done
echo.
if "%BUILD_EXIT_CODE%"=="0" (
    echo [Swim Engine] Linux clean build completed successfully.
) else (
    echo [Swim Engine] Linux clean build FAILED with exit code %BUILD_EXIT_CODE%.
)
echo.
pause
exit /b %BUILD_EXIT_CODE%
