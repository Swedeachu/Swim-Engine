<#
.SYNOPSIS
	One repeatable profile run of the engine: a fixed scene, GPU and CPU frame timings from the
	engine's `profile` command, and CPU stacks from SwimStackSampler (no administrator rights).

.DESCRIPTION
	1. Checks the build is Release with a PDB (and the sampler is built).
	2. Lists processes that would disturb the numbers (compilers, linkers, other engine or
	   game instances) and stops unless -AllowBusy: numbers from a busy machine are not comparable.
	3. Starts the engine on -Scene with vsync, validation and audio off, captures -Frames frames
	   after -Warmup with `profile ... quit`, and samples its threads for -SampleSeconds.
	4. Writes everything to -OutDir and summarizes it (Summarize-EngineProfile.py).
	A run is capped at -TimeoutSeconds (at most 300).

.EXAMPLE
	./scripts/profiling/Invoke-EngineProfile.ps1 -Label before
	./scripts/profiling/Invoke-EngineProfile.ps1 -Label after -Compare build/profiling/before
#>
param(
	[string]$BuildDir = "build/windows-release",
	[string]$Scene = "ProfileTerrain",
	[string]$Label = "run",
	[string]$OutDir = "",
	[string]$Size = "1920x1080",
	[int]$Frames = 3000,
	[int]$Warmup = 600,
	[double]$SampleDelaySeconds = 6,
	[double]$SampleSeconds = 20,
	[int]$SampleIntervalMs = 2,
	[int]$TimeoutSeconds = 240,
	[string[]]$Exec = @(),
	[string[]]$WindowArgs = @("--window-mode=windowed", "--no-activate", "--input=none"),
	[string]$Compare = "",
	[switch]$NoSampler,
	[switch]$AllowBusy
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "../..")
$build = if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $root $BuildDir }
$exe = Join-Path $build "Swim Engine.exe"
$sampler = Join-Path $build "SwimStackSampler.exe"
if (-not $OutDir) { $OutDir = Join-Path $root "build/profiling/$Label" }
$TimeoutSeconds = [Math]::Min($TimeoutSeconds, 300)

# 1. Release + PDB.
$cache = Join-Path $build "CMakeCache.txt"
if (-not (Test-Path $exe)) { throw "No engine at $exe. Build it: cmake --build $BuildDir --target SwimEngine" }
if (-not (Test-Path (Join-Path $build "Swim Engine.pdb"))) { throw "No PDB next to the engine: symbols are needed for the CPU report." }
if ((Test-Path $cache) -and -not (Select-String -Path $cache -Pattern "^CMAKE_BUILD_TYPE:STRING=Release$" -Quiet)) {
	throw "$BuildDir is not a Release build."
}
if (-not $NoSampler -and -not (Test-Path $sampler)) { throw "No sampler at $sampler. Build it: cmake --build $BuildDir --target SwimStackSampler" }

New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = Resolve-Path $OutDir

# 2. A quiet machine.
$noisy = "cl", "link", "lld-link", "ninja", "cmake", "msbuild", "dotnet", "ilc", "Swim Engine", "Swim Client", "Tungsten", "SwimTests"
$busy = Get-Process -ErrorAction SilentlyContinue | Where-Object { $noisy -contains $_.ProcessName }
$check = "Process check $(Get-Date -Format o): " + $(if ($busy) { ($busy | ForEach-Object { "$($_.ProcessName) ($($_.Id))" }) -join ", " } else { "none of $($noisy -join ', ') running" })
$check | Tee-Object -FilePath (Join-Path $OutDir "process-check.txt")
if ($busy -and -not $AllowBusy) { throw "The machine is busy (see above); wait for those to finish or pass -AllowBusy (numbers then are not comparable)." }

# 3. The run.
$csv = Join-Path $OutDir "frames.csv"
Remove-Item -ErrorAction SilentlyContinue $csv
$arguments = @("--scene=$Scene", "--size=$Size", "--no-vsync", "--validation=off", "--audio=off") + $WindowArgs
# The graphics-quality console variables (saved per user) can turn bloom back on: the profile
# scene runs without it.
if ($Scene -eq "ProfileTerrain") { $Exec = @("after 2 render.toggle post.bloom 0") + $Exec }
foreach ($e in $Exec) { $arguments += "--exec=`"$e`"" }
$arguments += "--exec=`"profile $Frames $Warmup $csv $Label quit`""
"Swim Engine $($arguments -join ' ')" | Set-Content (Join-Path $OutDir "command.txt")

$stdout = Join-Path $OutDir "engine.out"
$stderr = Join-Path $OutDir "engine.err"
$started = Get-Date
$engine = Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $build -NoNewWindow -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
$samplerProcess = $null
if (-not $NoSampler) {
	$samplerArgs = @("--pid", $engine.Id, "--delay", $SampleDelaySeconds, "--seconds", $SampleSeconds, "--interval-ms", $SampleIntervalMs,
		"--out", "`"$(Join-Path $OutDir 'cpu')`"", "--symbols", "`"$build`"")
	$samplerProcess = Start-Process -FilePath $sampler -ArgumentList $samplerArgs -NoNewWindow -PassThru `
		-RedirectStandardOutput (Join-Path $OutDir "sampler.out") -RedirectStandardError (Join-Path $OutDir "sampler.err")
}

if (-not $engine.WaitForExit($TimeoutSeconds * 1000)) {
	$engine.Kill()
	"Timed out after $TimeoutSeconds s: lower -Frames." | Tee-Object -Append -FilePath (Join-Path $OutDir "process-check.txt")
}
if ($samplerProcess) { $samplerProcess.WaitForExit(30000) | Out-Null }
"Run took $([int]((Get-Date) - $started).TotalSeconds) s"

# 4. Report.
$summarize = Join-Path $PSScriptRoot "Summarize-EngineProfile.py"
$summaryArgs = @($summarize, $OutDir)
if ($Compare) { $summaryArgs += @("--compare", $Compare) }
python @summaryArgs
