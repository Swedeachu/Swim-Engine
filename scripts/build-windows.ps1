param(
    [switch]$Debug
)

$ErrorActionPreference = "Stop"
$Preset = if ($Debug) { "windows-debug" } else { "windows-release" }

cmake --preset $Preset
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

cmake --build --preset $Preset
exit $LASTEXITCODE
