<#
.SYNOPSIS
    Builds the Debug configuration and runs the full Swim Engine Debug test set,
    including the Vulkan RHI smoke suites that only register when explicitly
    opted into.

.DESCRIPTION
    scripts/build-windows-soft.ps1 -Debug already builds the engine, runs the
    default SwimTests corpus and validates repository asset cooking. The RHI
    smoke suites are deliberately excluded from that default run: they need a
    real desktop session and a Vulkan-capable GPU, so they only register when
    SWIM_RUN_RHI_SMOKE=1 is present in the environment.

    This script performs the build step, then runs SwimTests a second time with
    the smoke gate opened, and restores the environment afterwards so an
    interactive PowerShell session is left exactly as it was found.

.EXAMPLE
    .\scripts\run-debug-tests.ps1
    Soft Debug build, default suite, then every RHI.Vulkan.Smoke case with core
    validation.

.EXAMPLE
    .\scripts\run-debug-tests.ps1 -SkipBuild -Validation all -StopOnFailure
    Reuse the existing build and run the smoke suites with synchronization and
    GPU-assisted validation, stopping at the first failure.

.EXAMPLE
    .\scripts\run-debug-tests.ps1 -SkipBuild -Filter @()
    Run the entire test corpus (default suites plus the smoke suites) in one
    pass against the existing build.
#>
param(
    # Reuse the existing build/windows-debug tree instead of building first.
    [switch]$SkipBuild,

    # Use the clean (network-enabled, dependency-refreshing) build script.
    [switch]$Clean,

    # SWIM_RHI_VALIDATION profile handed to the smoke fixtures.
    [ValidateSet("core", "sync", "gpu", "all")]
    [string]$Validation = "core",

    # Select an SDK root or x64 Bin folder. Otherwise discover the newest layer
    # in VULKAN_SDK, C:\VulkanSDK or .cache/vulkan-sdk, respecting VK_LAYER_PATH.
    [string]$VulkanSdkPath,

    # SwimTests --filter patterns for the opt-in pass. Pass @() to run everything.
    [string[]]$Filter = @("RHI.Vulkan.Smoke"),

    # Additional SwimTests --exclude patterns.
    [string[]]$Exclude = @(),

    # Print per-case check counts and timings.
    [switch]$StopOnFailure,
    [switch]$VerboseCases,

    # Run the selected set <Repeat> times.
    [ValidateRange(1, 1000)]
    [int]$Repeat = 1,

    # Write a JUnit XML report to this path.
    [string]$Report
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$BuildDirectory = Join-Path $Root "build/windows-debug"
. (Join-Path $PSScriptRoot 'vulkan-test-environment.ps1')

function Restore-SwimEnvironmentVariable {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [AllowNull()][object]$Value
    )

    # An absent variable reads back as $null; restoring it means removing it again.
    if ($null -eq $Value -or "$Value" -eq "") {
        Remove-Item -LiteralPath "Env:$Name" -ErrorAction SilentlyContinue
    }
    else {
        Set-Item -LiteralPath "Env:$Name" -Value ([string]$Value)
    }
}

Push-Location $Root

# Capture the caller's environment so the smoke gate never leaks out of this run.
$PreviousRunRhiSmoke = $env:SWIM_RUN_RHI_SMOKE
$PreviousRhiValidation = $env:SWIM_RHI_VALIDATION
$PreviousVkLayerPath = $env:VK_LAYER_PATH

try {
    $LayerDirectory = Get-SwimVulkanTestLayer -Root $Root -SdkPath $VulkanSdkPath -Validation $Validation
    if ($LayerDirectory) { $env:VK_LAYER_PATH = $LayerDirectory }

    if (-not $SkipBuild) {
        $BuildScript = if ($Clean) {
            Join-Path $PSScriptRoot "build-windows-clean.ps1"
        }
        else {
            Join-Path $PSScriptRoot "build-windows-soft.ps1"
        }

        Write-Host "[Swim] Debug build and default test suite: $BuildScript"
        & $BuildScript -Debug
        if ($LASTEXITCODE -ne 0) {
            throw "Debug build failed (exit code $LASTEXITCODE)."
        }
    }
    else {
        Write-Host "[Swim] Skipping the build; reusing $BuildDirectory."
    }

    $ExecutablePath = @(
        (Join-Path $BuildDirectory "SwimTests.exe"),
        (Join-Path $BuildDirectory "Debug/SwimTests.exe")
    ) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1

    if (-not $ExecutablePath) {
        throw "SwimTests.exe was not found under '$BuildDirectory'. Run this script without -SkipBuild first."
    }

    $TestArguments = @()
    foreach ($Pattern in $Filter) {
        if (-not [string]::IsNullOrWhiteSpace($Pattern)) {
            $TestArguments += "--filter=$Pattern"
        }
    }
    foreach ($Pattern in $Exclude) {
        if (-not [string]::IsNullOrWhiteSpace($Pattern)) {
            $TestArguments += "--exclude=$Pattern"
        }
    }
    if ($StopOnFailure) {
        $TestArguments += "--stop-on-failure"
    }
    if ($VerboseCases) {
        $TestArguments += "--verbose"
    }
    if ($Repeat -gt 1) {
        $TestArguments += "--repeat=$Repeat"
    }
    if ($Report) {
        $ReportDirectory = Split-Path -Parent $Report
        if ($ReportDirectory -and -not (Test-Path -LiteralPath $ReportDirectory -PathType Container)) {
            New-Item -ItemType Directory -Path $ReportDirectory -Force | Out-Null
        }
        $TestArguments += "--report=$Report"
    }

    # The RHI smoke suites register themselves only when this gate is open, so
    # an unavailable Vulkan device becomes a real failure instead of a silent skip.
    $env:SWIM_RUN_RHI_SMOKE = "1"
    $env:SWIM_RHI_VALIDATION = $Validation

    Write-Host "[Swim] SWIM_RUN_RHI_SMOKE=1, SWIM_RHI_VALIDATION=$Validation"
    Write-Host "[Swim] Running $ExecutablePath $($TestArguments -join ' ')"
    & $ExecutablePath @TestArguments
    $TestExitCode = $LASTEXITCODE
    if ($TestExitCode -ne 0) {
        throw "SwimTests reported failures (exit code $TestExitCode)."
    }

    Write-Host "[Swim] Debug test run passed." -ForegroundColor Green
    exit 0
}
catch {
    Write-Host "[Swim] ERROR: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
finally {
    Restore-SwimEnvironmentVariable -Name "SWIM_RUN_RHI_SMOKE" -Value $PreviousRunRhiSmoke
    Restore-SwimEnvironmentVariable -Name "SWIM_RHI_VALIDATION" -Value $PreviousRhiValidation
    Restore-SwimEnvironmentVariable -Name "VK_LAYER_PATH" -Value $PreviousVkLayerPath
    Pop-Location
}
