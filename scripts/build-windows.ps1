param(
    [switch]$Debug
)

# Backward-compatible entry point. Normal iterative builds are soft builds;
# use build-windows-clean.ps1 when dependencies/build state must be repulled.
& (Join-Path $PSScriptRoot "build-windows-soft.ps1") -Debug:$Debug
exit $LASTEXITCODE
