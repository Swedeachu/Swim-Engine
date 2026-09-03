$ErrorActionPreference = "Stop"


function ConvertTo-SwimExtendedWindowsPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $FullPath = [System.IO.Path]::GetFullPath($Path).TrimEnd('\')
    if ($FullPath.StartsWith('\\?\')) {
        return $FullPath
    }

    if ($FullPath.StartsWith('\\')) {
        return '\\?\UNC\' + $FullPath.Substring(2)
    }

    return '\\?\' + $FullPath
}

function Test-SwimPathEntryExists {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    # Test-Path/Get-Item can report false for a broken directory junction. For
    # generated-state cleanup we care whether the directory entry itself still
    # exists, not whether its target resolves, so enumerate only its short
    # parent directory and compare the final component directly.
    $FullPath = [System.IO.Path]::GetFullPath($Path).TrimEnd('\')
    $Parent = [System.IO.Path]::GetDirectoryName($FullPath)
    $Leaf = [System.IO.Path]::GetFileName($FullPath)
    if ([string]::IsNullOrWhiteSpace($Parent) -or [string]::IsNullOrWhiteSpace($Leaf)) {
        return $false
    }

    if (-not [System.IO.Directory]::Exists($Parent)) {
        return $false
    }

    try {
        foreach ($Entry in [System.IO.Directory]::EnumerateFileSystemEntries($Parent)) {
            $EntryLeaf = [System.IO.Path]::GetFileName($Entry.TrimEnd('\'))
            if ([System.StringComparer]::OrdinalIgnoreCase.Equals($EntryLeaf, $Leaf)) {
                return $true
            }
        }
    }
    catch {
        # The paths cleaned by Swim all have short parents. This fallback is
        # only for unusual filesystem providers/security software that blocks
        # enumeration while still allowing a direct lookup.
        try {
            return $null -ne (Get-Item -LiteralPath $Path -Force -ErrorAction Stop)
        }
        catch {
            return $false
        }
    }

    return $false
}

function Invoke-SwimWindowsDirectoryRemove {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [switch]$Recursive
    )

    # Windows PowerShell 5.1's Remove-Item still has several MAX_PATH and
    # partially-deleted-tree edge cases. Use the native directory remover with
    # an extended-length path instead. The \\?\\ prefix bypasses MAX_PATH for
    # local/UNC paths and cmd.exe's rd /s /q is tolerant of children that have
    # already disappeared during a previous failed cleanup.
    $ExtendedPath = ConvertTo-SwimExtendedWindowsPath -Path $Path
    if ($ExtendedPath.Contains('"')) {
        throw "Generated path contains an unsupported quote character: '$Path'."
    }

    $Command = if ($Recursive) {
        "rd /s /q `"$ExtendedPath`""
    }
    else {
        "rd `"$ExtendedPath`""
    }

    $StartInfo = New-Object System.Diagnostics.ProcessStartInfo
    $StartInfo.FileName = $env:ComSpec
    $StartInfo.Arguments = "/d /c $Command"
    $StartInfo.UseShellExecute = $false
    $StartInfo.CreateNoWindow = $true
    $StartInfo.RedirectStandardOutput = $false
    $StartInfo.RedirectStandardError = $true

    $Process = New-Object System.Diagnostics.Process
    $Process.StartInfo = $StartInfo
    if (-not $Process.Start()) {
        throw "Could not start Windows directory cleanup for '$Path'."
    }

    # Drain stderr while the process runs so a large access-denied report can
    # never fill the pipe and deadlock the cleanup process. rd /q produces no
    # useful stdout, so leave stdout unredirected.
    $StandardError = $Process.StandardError.ReadToEnd()
    $Process.WaitForExit()

    return [PSCustomObject]@{
        ExitCode = $Process.ExitCode
        StandardError = $StandardError.Trim()
    }
}

function Remove-SwimGeneratedDirectory {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [switch]$MayBeDirectoryLink
    )

    # Idempotency is required: a clean build may be re-run after an interrupted
    # or partially successful clean. "Already gone" is always success.
    if (-not (Test-SwimPathEntryExists -Path $Path)) {
        return
    }

    $LastRemovalResult = $null
    if ($MayBeDirectoryLink) {
        # Older Swim revisions used a junction at build/.px. First try plain
        # rd, which removes only the directory link and never traverses into
        # its target. Ignore a nonzero status here: a current .px is a normal
        # Git worktree and will intentionally require recursive deletion.
        $LastRemovalResult = Invoke-SwimWindowsDirectoryRemove -Path $Path
        if (-not (Test-SwimPathEntryExists -Path $Path)) {
            return
        }
    }

    $LastRemovalResult = Invoke-SwimWindowsDirectoryRemove -Path $Path -Recursive
    if (-not (Test-SwimPathEntryExists -Path $Path)) {
        return
    }

    $Detail = ""
    if ($LastRemovalResult -and -not [string]::IsNullOrWhiteSpace($LastRemovalResult.StandardError)) {
        $Detail = " Windows reported: $($LastRemovalResult.StandardError)"
    }

    throw "Failed to remove generated build state '$Path'.$Detail Close Visual Studio, terminals, Explorer windows, or other programs holding files under that path and retry."
}

function Get-SwimCommandPath {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Names
    )

    foreach ($Name in $Names) {
        $Command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($Command) {
            return $Command.Source
        }
    }

    return $null
}

function Get-SwimFirstExistingFile {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [string[]]$Candidates
    )

    foreach ($Candidate in $Candidates) {
        if ([string]::IsNullOrWhiteSpace($Candidate)) {
            continue
        }

        if (Test-Path -LiteralPath $Candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $Candidate).Path
        }
    }

    return $null
}

function Enable-SwimGitLongPaths {
    $ConfigCount = 0
    if ($env:GIT_CONFIG_COUNT -match '^\d+$') {
        $ConfigCount = [int]$env:GIT_CONFIG_COUNT
    }

    for ($Index = 0; $Index -lt $ConfigCount; ++$Index) {
        $ExistingKey = [Environment]::GetEnvironmentVariable("GIT_CONFIG_KEY_$Index", "Process")
        if ($ExistingKey -eq "core.longpaths") {
            [Environment]::SetEnvironmentVariable("GIT_CONFIG_VALUE_$Index", "true", "Process")
            return
        }
    }

    [Environment]::SetEnvironmentVariable("GIT_CONFIG_KEY_$ConfigCount", "core.longpaths", "Process")
    [Environment]::SetEnvironmentVariable("GIT_CONFIG_VALUE_$ConfigCount", "true", "Process")
    [Environment]::SetEnvironmentVariable("GIT_CONFIG_COUNT", ($ConfigCount + 1).ToString(), "Process")
}

function Get-SwimVsWherePath {
    $CommandPath = Get-SwimCommandPath -Names @("vswhere.exe", "vswhere")
    if ($CommandPath) {
        return $CommandPath
    }

    $ProgramFilesX86 = [Environment]::GetFolderPath("ProgramFilesX86")
    return Get-SwimFirstExistingFile -Candidates @(
        (Join-Path $ProgramFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe")
    )
}

function Get-SwimVisualStudioInstallation {
    $VsWhere = Get-SwimVsWherePath
    if ($VsWhere) {
        $Query = @("-latest", "-products", "*", "-version", "[17.0,18.0)", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath")
        $Result = & $VsWhere @Query 2>$null | Select-Object -First 1
        if ($LASTEXITCODE -eq 0 -and $Result -and (Test-Path -LiteralPath $Result -PathType Container)) {
            return (Resolve-Path -LiteralPath $Result).Path
        }
    }

    $Roots = @(
        (Join-Path ([Environment]::GetFolderPath("ProgramFiles")) "Microsoft Visual Studio\2022"),
        (Join-Path ([Environment]::GetFolderPath("ProgramFilesX86")) "Microsoft Visual Studio\2022")
    )

    foreach ($Root in $Roots) {
        if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
            continue
        }

        foreach ($Edition in @("Enterprise", "Professional", "Community", "BuildTools")) {
            $Candidate = Join-Path $Root $Edition
            $VcVars = Join-Path $Candidate "VC\Auxiliary\Build\vcvars64.bat"
            if (Test-Path -LiteralPath $VcVars -PathType Leaf) {
                return (Resolve-Path -LiteralPath $Candidate).Path
            }
        }
    }

    return $null
}

function Import-SwimMsvcEnvironment {
    param(
        [Parameter(Mandatory = $true)]
        [string]$VisualStudioInstallation
    )

    if (Get-SwimCommandPath -Names @("cl.exe", "cl")) {
        return
    }

    $VsDevCmd = Join-Path $VisualStudioInstallation "Common7\Tools\VsDevCmd.bat"
    $VcVars64 = Join-Path $VisualStudioInstallation "VC\Auxiliary\Build\vcvars64.bat"

    if (Test-Path -LiteralPath $VsDevCmd -PathType Leaf) {
        $SetupCommand = "`"$VsDevCmd`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
    }
    elseif (Test-Path -LiteralPath $VcVars64 -PathType Leaf) {
        $SetupCommand = "`"$VcVars64`" >nul && set"
    }
    else {
        throw "Visual Studio was found at '$VisualStudioInstallation', but its x64 C++ environment scripts are missing. Install the Desktop development with C++ workload."
    }

    $EnvironmentLines = & $env:ComSpec /d /s /c $SetupCommand
    if ($LASTEXITCODE -ne 0) {
        throw "Visual Studio's x64 C++ environment failed to initialize (exit code $LASTEXITCODE)."
    }

    foreach ($Line in $EnvironmentLines) {
        if ($Line -match '^([^=]+)=(.*)$') {
            [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], "Process")
        }
    }

    if (-not (Get-SwimCommandPath -Names @("cl.exe", "cl"))) {
        throw "Visual Studio's x64 C++ environment initialized, but cl.exe is still unavailable. Repair/install the MSVC x64/x86 build tools."
    }
}

function Get-SwimCMakePath {
    param(
        [AllowNull()]
        [string]$VisualStudioInstallation
    )

    $CommandPath = Get-SwimCommandPath -Names @("cmake.exe", "cmake")
    if ($CommandPath) {
        return $CommandPath
    }

    $Candidates = @()
    if ($VisualStudioInstallation) {
        $Candidates += Join-Path $VisualStudioInstallation "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    }

    $Candidates += Join-Path ([Environment]::GetFolderPath("ProgramFiles")) "CMake\bin\cmake.exe"
    $Candidates += Join-Path ([Environment]::GetFolderPath("ProgramFilesX86")) "CMake\bin\cmake.exe"

    return Get-SwimFirstExistingFile -Candidates $Candidates
}

function Get-SwimNinjaPath {
    param(
        [AllowNull()]
        [string]$VisualStudioInstallation
    )

    $CommandPath = Get-SwimCommandPath -Names @("ninja.exe", "ninja", "ninja-build.exe", "ninja-build")
    if ($CommandPath) {
        return $CommandPath
    }

    $Candidates = @()
    if ($VisualStudioInstallation) {
        $Candidates += Join-Path $VisualStudioInstallation "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
    }

    $Candidates += Join-Path ([Environment]::GetFolderPath("ProgramFiles")) "CMake\bin\ninja.exe"
    $Candidates += "C:\ProgramData\chocolatey\bin\ninja.exe"

    return Get-SwimFirstExistingFile -Candidates $Candidates
}

function Get-SwimWindowsBuildPlan {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Root,

        [switch]$DebugBuild
    )

    Enable-SwimGitLongPaths

    $VisualStudioInstallation = Get-SwimVisualStudioInstallation
    $CMakePath = Get-SwimCMakePath -VisualStudioInstallation $VisualStudioInstallation
    if (-not $CMakePath) {
        throw "CMake was not found. Install CMake or Visual Studio 2022 with the C++ CMake tools component; the build scripts automatically discover either installation."
    }

    $NinjaPath = Get-SwimNinjaPath -VisualStudioInstallation $VisualStudioInstallation
    if ($NinjaPath) {
        if (-not (Get-SwimCommandPath -Names @("cl.exe", "cl"))) {
            if (-not $VisualStudioInstallation) {
                throw "Ninja was found, but the MSVC compiler was not. Install Visual Studio 2022/Build Tools with Desktop development with C++."
            }

            Write-Host "[Swim] Initializing the Visual Studio x64 C++ environment."
            Import-SwimMsvcEnvironment -VisualStudioInstallation $VisualStudioInstallation
        }

        $NinjaDirectory = Split-Path -Parent $NinjaPath
        if (-not (($env:Path -split ';') -contains $NinjaDirectory)) {
            $env:Path = "$NinjaDirectory;$env:Path"
        }

        $Preset = if ($DebugBuild) { "windows-debug" } else { "windows-release" }
        Write-Host "[Swim] Toolchain: Ninja + MSVC"
        Write-Host "[Swim] CMake: $CMakePath"
        Write-Host "[Swim] Ninja: $NinjaPath"
        if ($VisualStudioInstallation) {
            Write-Host "[Swim] Visual Studio: $VisualStudioInstallation"
        }

        return [PSCustomObject]@{
            CMakePath = $CMakePath
            ConfigurePreset = $Preset
            BuildPreset = $Preset
            BuildDirectory = Join-Path $Root "build/$Preset"
            ConfigureArguments = @("-DCMAKE_MAKE_PROGRAM=$NinjaPath")
            Generator = "Ninja"
        }
    }

    if (-not $VisualStudioInstallation) {
        throw "Neither Ninja nor a usable Visual Studio 2022 C++ installation was found. Install Visual Studio 2022/Build Tools with Desktop development with C++; Ninja itself is optional because the script can fall back to the Visual Studio generator."
    }

    $BuildPreset = if ($DebugBuild) { "windows-vs-debug" } else { "windows-vs" }
    Write-Host "[Swim] Ninja was not found; using the Visual Studio generator fallback."
    Write-Host "[Swim] CMake: $CMakePath"
    Write-Host "[Swim] Visual Studio: $VisualStudioInstallation"

    return [PSCustomObject]@{
        CMakePath = $CMakePath
        ConfigurePreset = "windows-vs"
        BuildPreset = $BuildPreset
        BuildDirectory = Join-Path $Root "build/windows-vs"
        ConfigureArguments = @()
        Generator = "Visual Studio 17 2022"
    }
}
