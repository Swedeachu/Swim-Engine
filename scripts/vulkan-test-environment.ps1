# Select an SDK layer for this test process only. Never install an SDK or alter
# the machine registry, driver, or persistent environment from the test runner.
function Get-SwimVulkanTestLayer {
    param([string]$Root, [string]$SdkPath, [string]$Validation)

    if (-not $SdkPath -and $env:VK_LAYER_PATH) {
        Write-Host "[Swim] Using caller's VK_LAYER_PATH=$env:VK_LAYER_PATH"
        return $null
    }

    $Candidates = @()
    if ($SdkPath) {
        $Candidates += $SdkPath
    }
    else {
        if ($env:VULKAN_SDK) { $Candidates += $env:VULKAN_SDK }
        foreach ($Directory in @((Join-Path $Root '.cache/vulkan-sdk'), 'C:\VulkanSDK')) {
            if (Test-Path -LiteralPath $Directory -PathType Container) {
                $Candidates += Get-ChildItem -LiteralPath $Directory -Directory | Select-Object -ExpandProperty FullName
            }
        }
    }

    $Layers = @(foreach ($Candidate in ($Candidates | Select-Object -Unique)) {
        foreach ($Directory in @((Join-Path $Candidate 'Bin'), $Candidate)) {
            $Manifest = Join-Path $Directory 'VkLayer_khronos_validation.json'
            if (-not (Test-Path -LiteralPath $Manifest -PathType Leaf)) { continue }
            $Layer = (Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json).layer
            $Library = if ([IO.Path]::IsPathRooted($Layer.library_path)) {
                $Layer.library_path
            } else { Join-Path $Directory $Layer.library_path }
            if ($Layer.name -eq 'VK_LAYER_KHRONOS_validation' -and (Test-Path -LiteralPath $Library -PathType Leaf)) {
                [PSCustomObject]@{ Directory = (Resolve-Path -LiteralPath $Directory).Path; Version = [version]$Layer.api_version }
            }
        }
    })
    $Selected = $Layers | Sort-Object Version -Descending | Select-Object -First 1
    if (-not $Selected) {
        if ($SdkPath) { throw "No x64 Khronos validation layer manifest/DLL found in '$SdkPath' or its Bin directory." }
        Write-Host '[Swim] No SDK layer discovered; using the Vulkan loader configuration.'
        return $null
    }
    $MinimumVersion = if ($Validation -in @('gpu', 'all')) { [version]'1.4.350' } else { [version]'1.4.335' }
    if ($Validation -ne 'core' -and $Selected.Version -lt $MinimumVersion) {
        throw "Validation '$Validation' requires layer $MinimumVersion or newer; found $($Selected.Version) in '$($Selected.Directory)'. Install a current Vulkan SDK, or pass -VulkanSdkPath <SDK-root-or-Bin>. Updating a GPU driver does not update the SDK layer."
    }
    Write-Host "[Swim] Khronos validation layer $($Selected.Version): $($Selected.Directory)"
    return $Selected.Directory
}
