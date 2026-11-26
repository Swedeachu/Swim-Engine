# Profiling tools

Small, engine-generic helpers for profiling an application built on the engine. The full
scripted run of the engine's own profile scene is `scripts/profiling/Invoke-EngineProfile.ps1`.

## Shader source in GPU profilers

GPU profilers (Nsight Graphics shader profiler, GPU Trace hotspots) map costs to `.slang`
lines only when the SPIR-V carries `NonSemantic.Shader.DebugInfo` with the sources embedded.
Configure with the option on and rebuild the shaders:

```
cmake -S . -B build/windows-release -DSWIM_SHADER_DEBUG_INFO=ON
cmake --build build/windows-release --target SwimRuntimeShaders
```

or set the environment variable `SWIM_SHADER_DEBUG_INFO=1` before the first configure (it
sets the option's default). Shaders keep the default optimisation level, so the profiled code
is the release code; only the files grow. Shader hot reload adds the same flags when the
variable is set at run time. Turn it off again for shipping builds.

## Engine frame statistics

The `profile <frames> <warmup> <csv> [label]` console command captures per-zone statistics
(categories `frame`, `cpu`, `render`, `record`, `feature`, `gpu`) to a CSV. The renderer's CPU
breakdown in it comes from `FrameStats::CpuPhases`, `RecordPhases` (executor steps),
`RecordPasses` (each pass callback) and `FeaturePhases`.

```
python tools/profiling/profile_csv.py frames.csv --top 20
python tools/profiling/profile_csv.py after.csv --compare before.csv --category gpu
```

## Nsight Graphics exports

Export the GPU Trace range/action tables or the shader profiler's source view as CSV, then:

```
python tools/profiling/nsight_summary.py ranges.csv actions.csv shader-lines.csv --top 30 --frames 300
```

Columns are found by name (name/range/marker, duration/time with its unit, samples or %, file
and line), and rows with the same name are summed and ranked.

## CPU symbols

```
. ./tools/profiling/Set-ProfilingSymbols.ps1 -Executable "<build>/<app>.exe"
```

sets `_NT_SYMBOL_PATH` (local PDB directories, then the Microsoft symbol server with a local
cache) for the profiler started from that shell, and warns when the PDB is missing or stale.

## Measuring well

- One profile at a time on a quiet machine: no compiles, no second instance.
- Short runs (30 s) after a warm-up; compare runs with the same scene, size and settings.
- vsync off, validation off, a Release build with symbols.
