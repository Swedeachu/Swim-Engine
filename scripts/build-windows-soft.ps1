param(
    [switch]$Debug
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$DependencyCache = Join-Path $Root ".cache/cpm"
$VisualStudioBuildDirectory = Join-Path $Root "build/windows-vs"
$VisualStudioSolution = Join-Path $VisualStudioBuildDirectory "SwimEngine.sln"

. (Join-Path $PSScriptRoot "windows-build-common.ps1")

Push-Location $Root
try {
    $CpmBootstrap = Get-ChildItem -Path (Join-Path $DependencyCache "cpm") -Filter "CPM_*.cmake" -File -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $CpmBootstrap) {
        throw "Dependency cache is not bootstrapped. Run scripts/build-windows-clean.bat once with network access before using a soft build."
    }

    $BuildPlan = Get-SwimWindowsBuildPlan -Root $Root -DebugBuild:$Debug

    Write-Host "[Swim] Soft Windows build: $($BuildPlan.BuildPreset)"
    Write-Host "[Swim] Dependency downloads are disabled; cached CPM sources will be reused."

    $ConfigureArguments = @("--preset", $BuildPlan.ConfigurePreset, "-DFETCHCONTENT_FULLY_DISCONNECTED=ON") + $BuildPlan.ConfigureArguments
    & $BuildPlan.CMakePath @ConfigureArguments
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    if ($BuildPlan.ConfigurePreset -ne "windows-vs") {
        Write-Host "[Swim] Refreshing Visual Studio 2022 solution: $VisualStudioSolution"
        Write-Host "[Swim] Solution refresh is fully disconnected and reuses the validated dependency cache."
        & $BuildPlan.CMakePath --preset windows-vs -DFETCHCONTENT_FULLY_DISCONNECTED=ON
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    }

    if (-not (Test-Path -LiteralPath $VisualStudioSolution -PathType Leaf)) {
        throw "Visual Studio solution refresh completed without producing '$VisualStudioSolution'."
    }

    Assert-SwimVisualStudioSolutionLayout -SolutionPath $VisualStudioSolution
    Write-Host "[Swim] Visual Studio solution synchronized and organized: $VisualStudioSolution"
    & $BuildPlan.CMakePath --build --preset $BuildPlan.BuildPreset --parallel
    exit $LASTEXITCODE
}
catch {
    Write-Host "[Swim] ERROR: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
finally {
    Pop-Location
}
