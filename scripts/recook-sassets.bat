@echo off
setlocal EnableExtensions EnableDelayedExpansion

for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set "ASSETS=%ROOT%\Assets"
set "BUILD_ROOT=%ROOT%\build"
set "COOKER="
set "SYNC_FAILED=0"

if not exist "%ASSETS%\" (
    echo [Swim] ERROR: Repository asset root does not exist: %ASSETS%
    exit /b 1
)

call :find_cooker
if not defined COOKER (
    call :try_build_cooker
    if errorlevel 1 (
        echo [Swim] ERROR: Could not find or build SwimAssetCooker. Configure/build a Windows tree first, for example build\windows-release.
        exit /b 1
    )
)

echo [Swim] Asset cooker: %COOKER%
echo [Swim] Removing previous cooked output: %ASSETS%\Cooked
if exist "%ASSETS%\Cooked\" (
    rmdir /s /q "%ASSETS%\Cooked"
    if exist "%ASSETS%\Cooked\" (
        echo [Swim] ERROR: Failed to remove previous cooked output: %ASSETS%\Cooked
        exit /b 1
    )
)

echo [Swim] Fully recooking repository assets.
"%COOKER%" "%ASSETS%"
if errorlevel 1 (
    echo [Swim] ERROR: SwimAssetCooker failed.
    exit /b 1
)

if not exist "%ASSETS%\Cooked\" (
    echo [Swim] ERROR: SwimAssetCooker succeeded but did not create %ASSETS%\Cooked
    exit /b 1
)

set /a SASSET_COUNT=0
for /r "%ASSETS%\Cooked" %%F in (*.sasset) do set /a SASSET_COUNT+=1
echo [Swim] Fresh repository cook contains !SASSET_COUNT! .sasset files.

rem Development builds read the repository's Assets\ directly (SWIM_DEVELOPMENT_ASSET_ROOT),
rem so nothing is copied next to the executables. Packaged runs (SWIM_DEPLOY_ASSETS=ON) copy
rem Assets\ when SwimEngine is built.
echo [Swim] Sasset recook completed successfully.
exit /b 0

:find_cooker
set "COOKER="
for %%F in (
    "%BUILD_ROOT%\windows-release\SwimAssetCooker.exe"
    "%BUILD_ROOT%\windows-debug\SwimAssetCooker.exe"
    "%BUILD_ROOT%\windows-vs\Release\SwimAssetCooker.exe"
    "%BUILD_ROOT%\windows-vs\Debug\SwimAssetCooker.exe"
) do (
    if not defined COOKER if exist "%%~fF" set "COOKER=%%~fF"
)

if not defined COOKER if exist "%BUILD_ROOT%\" (
    for /r "%BUILD_ROOT%" %%F in (SwimAssetCooker.exe) do (
        if not defined COOKER set "COOKER=%%~fF"
    )
)
exit /b 0

:try_build_cooker
where cmake.exe >nul 2>nul
if errorlevel 1 exit /b 1

if exist "%BUILD_ROOT%\windows-release\CMakeCache.txt" (
    echo [Swim] SwimAssetCooker is not built; building it in: %BUILD_ROOT%\windows-release
    cmake.exe --build "%BUILD_ROOT%\windows-release" --target SwimAssetCooker --parallel
    if not errorlevel 1 (
        call :find_cooker
        if defined COOKER exit /b 0
    )
)

if exist "%BUILD_ROOT%\windows-debug\CMakeCache.txt" (
    echo [Swim] SwimAssetCooker is not built; building it in: %BUILD_ROOT%\windows-debug
    cmake.exe --build "%BUILD_ROOT%\windows-debug" --target SwimAssetCooker --parallel
    if not errorlevel 1 (
        call :find_cooker
        if defined COOKER exit /b 0
    )
)

if exist "%BUILD_ROOT%\windows-vs\CMakeCache.txt" (
    echo [Swim] SwimAssetCooker is not built; building it in: %BUILD_ROOT%\windows-vs ^(Release^)
    cmake.exe --build "%BUILD_ROOT%\windows-vs" --config Release --target SwimAssetCooker --parallel
    if not errorlevel 1 (
        call :find_cooker
        if defined COOKER exit /b 0
    )
)

exit /b 1
