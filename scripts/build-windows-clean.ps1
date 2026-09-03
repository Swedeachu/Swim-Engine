param(
    [switch]$Debug
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$DependencyCacheRoot = Join-Path $Root ".cache"
$PhysXShortWorktree = Join-Path $Root "build/.px"
$VisualStudioBuildDirectory = Join-Path $Root "build/windows-vs"
$VisualStudioSolution = Join-Path $VisualStudioBuildDirectory "SwimEngine.sln"
$WindowsBuildDirectories = @(
    (Join-Path $Root "build/windows-release"),
    (Join-Path $Root "build/windows-debug"),
    $VisualStudioBuildDirectory
)

. (Join-Path $PSScriptRoot "windows-build-common.ps1")

Push-Location $Root
try {
    $BuildPlan = Get-SwimWindowsBuildPlan -Root $Root -DebugBuild:$Debug

    Write-Host "[Swim] Clean Windows build: $($BuildPlan.BuildPreset)"
    Write-Host "[Swim] Removing shared PhysX worktree/legacy junction: $PhysXShortWorktree"
    Remove-SwimGeneratedDirectory -Path $PhysXShortWorktree -MayBeDirectoryLink

    foreach ($BuildDirectory in $WindowsBuildDirectories) {
        Write-Host "[Swim] Removing Windows build tree: $BuildDirectory"
        Remove-SwimGeneratedDirectory -Path $BuildDirectory
    }

    Write-Host "[Swim] Removing complete repository dependency cache: $DependencyCacheRoot"
    Remove-SwimGeneratedDirectory -Path $DependencyCacheRoot

    # A clean build is a state reset, not merely a configure with fetching
    # enabled. Refuse to continue if any shared generated dependency state
    # survived deletion; otherwise stale aliases/caches can silently point at a
    # checkout that no longer exists and fail much later in the build.
    foreach ($RemovedPath in @($PhysXShortWorktree, $DependencyCacheRoot) + $WindowsBuildDirectories) {
        if (Test-SwimPathEntryExists -Path $RemovedPath) {
            throw "Clean-build reset left generated state behind at '$RemovedPath'."
        }
    }

    Write-Host "[Swim] Clean state verified. Pulling every pinned dependency from scratch."
    $ConfigureArguments = @("--preset", $BuildPlan.ConfigurePreset, "-DFETCHCONTENT_FULLY_DISCONNECTED=OFF") + $BuildPlan.ConfigureArguments
    & $BuildPlan.CMakePath @ConfigureArguments
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    if ($BuildPlan.ConfigurePreset -ne "windows-vs") {
        Write-Host "[Swim] Generating Visual Studio 2022 solution: $VisualStudioSolution"
        Write-Host "[Swim] Reusing the freshly populated, integrity-checked dependency cache; no second dependency pull is allowed."
        & $BuildPlan.CMakePath --preset windows-vs -DFETCHCONTENT_FULLY_DISCONNECTED=ON
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    }

    if (-not (Test-Path -LiteralPath $VisualStudioSolution -PathType Leaf)) {
        throw "Visual Studio solution generation completed without producing '$VisualStudioSolution'."
    }

    Write-Host "[Swim] Visual Studio solution ready: $VisualStudioSolution"
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
