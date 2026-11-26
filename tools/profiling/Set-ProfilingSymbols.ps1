<#
.SYNOPSIS
	Sets up symbols for CPU profilers and debuggers (this PowerShell session only).

.DESCRIPTION
	Puts every directory that holds a PDB of the given executable's build first on
	_NT_SYMBOL_PATH, followed by the Microsoft symbol server with a local cache, and checks
	that the executable has a matching PDB next to it. Run it in the shell that starts the
	profiler (WPA, Superluminal, Nsight Systems, a debugger).

.EXAMPLE
	. ./tools/profiling/Set-ProfilingSymbols.ps1 -Executable "build/windows-release/Swim Engine.exe"
#>
param(
	[Parameter(Mandatory = $true)][string]$Executable,
	[string]$Cache = (Join-Path $env:LOCALAPPDATA "SymbolCache")
)

$ErrorActionPreference = "Stop"
$exe = Resolve-Path $Executable
$dir = Split-Path $exe
$pdb = [IO.Path]::ChangeExtension($exe, ".pdb")

if (-not (Test-Path $pdb)) {
	Write-Warning "No PDB next to $exe; CPU stacks will show addresses. Build with debug info (RelWithDebInfo, or /Zi + /DEBUG)."
}
elseif ((Get-Item $pdb).LastWriteTime -lt (Get-Item $exe).LastWriteTime.AddMinutes(-5)) {
	Write-Warning "The PDB is older than the executable; it may not match."
}

$dirs = @($dir) + (Get-ChildItem -Path $dir -Filter *.pdb -Recurse -Depth 2 -ErrorAction SilentlyContinue |
	ForEach-Object { $_.DirectoryName } | Sort-Object -Unique)
New-Item -ItemType Directory -Force $Cache | Out-Null
$env:_NT_SYMBOL_PATH = (($dirs | Sort-Object -Unique) -join ";") + ";srv*$Cache*https://msdl.microsoft.com/download/symbols"
"_NT_SYMBOL_PATH=$env:_NT_SYMBOL_PATH"
