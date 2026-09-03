$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$DependencyCache = Join-Path $Root ".cache/cpm"
$VisualStudioSolution = Join-Path $Root "build/windows-vs/SwimEngine.sln"

. (Join-Path $PSScriptRoot "windows-build-common.ps1")

Push-Location $Root
try {
    $BuildPlan = Get-SwimWindowsBuildPlan -Root $Root
    $CpmBootstrap = Get-ChildItem -Path (Join-Path $DependencyCache "cpm") -Filter "CPM_*.cmake" -File -ErrorAction SilentlyContinue | Select-Object -First 1
    $Disconnected = if ($CpmBootstrap) { "ON" } else { "OFF" }

    if ($Disconnected -eq "ON") {
        Write-Host "[Swim] Generating organized Visual Studio solution from the existing dependency cache."
    }
    else {
        Write-Host "[Swim] Dependency cache is empty; generating the organized Visual Studio solution with dependency fetching enabled."
    }

    & $BuildPlan.CMakePath --preset windows-vs "-DFETCHCONTENT_FULLY_DISCONNECTED=$Disconnected"
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    Assert-SwimVisualStudioSolutionLayout -SolutionPath $VisualStudioSolution
    Write-Host "[Swim] Organized Visual Studio solution ready: $VisualStudioSolution"
    exit 0
}
catch {
    Write-Host "[Swim] ERROR: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
finally {
    Pop-Location
}
