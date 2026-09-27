# Profiling and the 240 FPS work (Phase 23)

Every rendering feature, pass and scene part can be switched off at runtime, every frame
is timed per CPU zone and per GPU pass, and scripted benchmarks write CSVs, so each slice
of the frame can be measured on its own. This page describes the tools, the scenarios and
the measurements, and keeps the optimization log.

Reference machine: RTX 4070 Laptop GPU, 1920 x 1080, Release, `--validation=off`, headless
(offscreen, no present wait). Frame times are GPU-bound in every scenario; CPU time per
frame (update, extraction, graph build and recording) is about 2 ms and overlaps the GPU
(two frames in flight).

## Tools

### Profiling switches (`render.toggles`, `render.toggle`)

`RenderToggles` (Engine/Runtime/RenderToggles.h) is a registry of named on/off switches.
The engine registers the renderer's own, render features register one each
(`feature.<name>`), and scenes register their parts (the sandbox: `scene.sponza`,
`scene.light-swarm`, ...). The sandbox's Profiling panel (Rendering tab) shows them all.

| Group | Switches |
| --- | --- |
| lighting | `ibl`, `local-lights`, `deferred-local-lights`, `environment-updates` |
| sky | `background` |
| shadows | `all`, `sun`, `spot`, `point` (budgets), `cascade-cache` |
| forward | `transparent` |
| screen | `ao`, `ssr`, `ssr-history`, `ssr-back-faces`, `fog` |
| reflections | `probes` |
| temporal | `taa` |
| effects | `particles` |
| post | `bloom`, `auto-exposure` |
| ui | `draw` |
| feature | one per render feature (clouds, sun shafts, lens flare, lensing, depth of field, camera lens, film) |
| scene | sandbox parts (Sponza, light swarm and its motion, instance hall, PBR gallery, physics toys, glass, emissive, tentacles, reflection lab) |

```text
render.toggles [filter]            list (with state)
render.toggle <name> [0|1]         flip or set one
render.toggle screen.* 0           a whole group
render.toggle all 1                everything back on
```

### Quality knobs (`render.set`)

Numeric settings that trade cost for quality, live: `cluster.tile`, `cluster.slices`,
`cluster.far`, `ssr.steps`, `ssr.stride`, `ssr.refine`, `ssr.distance`, `ssr.roughness`,
`ssr.half`, `ao.slices`, `ao.steps`, `ao.radius`, `ao.half`, `shadow.cascade`,
`shadow.spot`, `shadow.point`, `shadow.spots`, `shadow.points`, `shadow.pcf`,
`probes.resolution`, `probes.faces`, `probes.filters`, `probes.samples`, `probes.idle`.
`render.set` alone lists them with their values.

### Frame profiler (`profile`)

`FrameProfiler` (Engine/Runtime/FrameProfiler.h) collects, per frame:

- `cpu` zones the engine times (platform, input, UI, fixed steps, physics, behaviours,
  render bridge, renderer total, ...);
- `render` phases of the renderer's CPU work (wait for the previous frame, build phases,
  graph compile, record and submit) and `record` passes (CPU recording time per pass);
- `gpu` passes: each pass's exclusive GPU time from timestamps (sums of same-named passes,
  such as the six probe faces);
- `frame`: wall time, tick CPU time, and the GPU span (first begin to last end).

`profile <frames> [warmup] [csv|-] [label] [quit]` skips `warmup` frames, measures
`frames`, prints the costliest zones per category and appends mean / min / p50 / p95 / p99 /
max per zone to the CSV (`label,category,name,frames,per_frame_ms,mean_ms,...`).

### Benchmarks (`bench`)

`bench <csv> <frames> <warmup> <ablate|base> <label=cmd;cmd|label=cmd...> [quit]` runs
scenarios back to back. For each scenario it runs its commands (a camera bookmark, toggles,
knobs), then captures a profile labelled `<label>|baseline`. With `ablate` it then captures
once per profiling switch turned off alone (`<label>|-<switch>`), turning it back on after.

```sh
"Swim Engine" --headless --size=1920x1080 --frames=1000000 --validation=off \
  --exec="sandbox.hud 0" \
  --exec="bench prof/base.csv 180 60 base overview=sandbox.view 0|atrium=sandbox.view 5|lab=sandbox.view 7 quit"
```

A sweep of one knob is a list of scenarios that set it (`t32=sandbox.view 5;render.set cluster.tile 32|t64=...`).
Timings drift by a few tenths of a millisecond between runs (probe scheduling, swarm
motion, clocks): compare repeated baselines, not single numbers.

## Scenarios

The sandbox camera bookmarks used for every measurement:

| Label | View | What it stresses |
| --- | --- | --- |
| overview | `sandbox.view 0` | The playground: every feature at moderate cost |
| hall | `sandbox.view 2` | 2,000 instanced cubes |
| atrium | `sandbox.view 5` | Inside Sponza: 256 moving point lights, alpha-tested foliage, shadows |
| above | `sandbox.view 6` | Sponza from above: long SSR rays over the whole frame |
| lab | `sandbox.view 7` | The reflection lab: mirrors, probes, SSR |
| chrome | `sandbox.view 8` | Chrome spheres: probes and SSR |
| hole | `sandbox.view 9` | The black hole filling the frame |

## Results

GPU frame time in milliseconds (RTX 4070 Laptop, 1080p). "Start" is the first measurement
of this phase (after the CPU recording work: descriptor pools, view cache, query pool
reuse, two frames in flight).

| Scenario | Before the phase | Start | Now | FPS now |
| --- | --- | --- | --- | --- |
| overview | 14.9 | 7.3 | 6.7 | 150 |
| hall | – | 7.7 | 6.0 | 165 |
| atrium | 23.0 | 14.0 | 10.1 | 99 |
| above | – | 13.3 | 9.1 | 110 |
| lab | 17.2 | 11.0 | 8.4 | 119 |
| chrome | – | 8.8 | 7.0 | 143 |
| hole | 15.9 | 9.2 | 9.9 | 101 |

(The hole view got more expensive than at the start because the black hole now traces the
scene along each bent ray instead of one lookup: that removed its cut-offs.)

240 FPS (4.17 ms) is not reached in any sandbox view yet: the cheapest views are at about
6 ms. The table below splits the remaining time.

## What was measured and changed

Each change was measured with `bench` against its switch or knob (same build, same run,
alternating baselines).

| Change | Where | Saved (GPU ms) |
| --- | --- | --- |
| Half-resolution screen-space reflections: one pixel of each 2 x 2 block traced per frame (rotating), resolved by a depth- and normal-aware filter in the composite, TAA gathers the block (`ReflectionSettings::HalfResolution`) | SSR 2.6 -> 0.9 (atrium), 4.4 -> 1.3 (above) | 1.2 - 3.2 |
| Deferred local lights: the opaque pass shades everything but the clustered point/spot lights and writes base colour + metalness; a compute pass adds the lights per pixel, walking the wave's union of cluster masks (scalarized) (`RenderSettings::DeferredLocalLights`) | Forward+ 3.9 -> 1.0 + 1.4 (atrium) | 0.9 - 1.5 in Sponza, neutral elsewhere |
| Half-resolution GTAO with a 3 x 3 depth-aware resample in the blur (`AmbientOcclusionSettings::HalfResolution`) | AO + blur 1.05 -> 0.43 | 0.3 - 0.7 |
| Sky pass writes colour only; the opaque pass clears the other six targets (`ForwardPlusTargets::ClearAuxiliary`) | Sky 0.34 -> 0.06 | 0.3 |
| Word-major cluster mask threads (each wave shares its 32 lights: broadcast loads) | Masks 0.5 -> 0.18 (32 px tiles), 1.8 -> 0.6 (16 px) | 0.3 |
| 32-pixel light clusters in the sandbox | Forward+ | ~0.3 (atrium) |
| Probe faces without movers refresh every 30 frames (`ReflectionProbeSettings::IdleRefreshFrames`; faces seeing movers still go first every frame) | Probe capture | 0 - 0.4 (view-dependent) |
| Composite skips the probe parallax march where SSR is at least 97 % confident | Composite | 0.1 - 0.3 |
| Forward+ light loop: range test before the light's direction and cone, no shadow lookup for back-facing lights | Forward+ | small |
| Sun cascade cache: cascade 1 every second frame, 2+ every fourth, sooner when the camera or sun moves (`RenderSettings::ShadowCascadeCache`) | Shadows 1.58 -> 1.46 (atrium), 1.19 -> 1.02 (above) | 0.1 - 0.2 |
| Lensing: depth-only tests per step (colour fetched once at the end), gas frame per pixel instead of per step | Lensing | small (see below) |

Earlier in the phase (CPU): descriptor pool recycling, a texture view cache, timestamp query
pool reuse, O(n) subresource validation and two frames in flight took CPU recording from
4.5 to 2.0 ms, so the frame is GPU-bound everywhere.

Measured and kept as they were (quality):

- 2048 instead of 4096 sun cascades: -0.3 to -0.9 ms in Sponza (the cascade size was chosen for sharp playground shadows).
- One GTAO slice instead of two: -0.3 ms before the half-resolution change.
- Three probe faces per frame instead of six: -0.5 ms, but slower reflection updates of moving objects.

Correctness checks: every change keeps the unit suite green (903 on Windows, 895 on
Linux) and the screen-space smokes; captures with and without each switch differ only
where expected (deferred vs forward lights: 0.4 % of pixels in the atrium differ by more
than 8/255, on animated foliage; half vs full AO: 0.9 %).

## Where the time goes now

Atrium (Sponza, 256 moving lights), 10.0 ms of GPU passes:

| Pass | ms |
| --- | --- |
| Shadows depth (4096 cascades over Sponza, masked foliage) | 1.46 |
| Local lights (compute) | 1.37 |
| Screen-space composite (reflection resolve, probes, fog) | 1.08 |
| Forward+ opaque | 1.01 |
| Screen-space reflections (half resolution) | 0.90 |
| Probe faces (depth + opaque, six faces at 256) | 0.74 |
| Depth prepass + back-face depth | 0.51 |
| Clouds, bloom, TAA, AO, cluster masks, probe filtering, post | ~2.0 |

Black hole view: the lensing pass is 3.6 ms (every pixel in the hole's region traces up to
240 geodesic steps through the gas, testing the depth buffer at each), clouds 1.3 ms.

Next steps, in expected order of gain:

1. Lensing at lower cost: test the depth buffer every other step (the test's thickness
   already covers the step), fewer gas steps with a per-pixel jitter the TAA resolves, and
   early termination once the ray leaves the gas and the screen.
2. Shadows: cache static casters per cascade (redraw only dynamic casters over a copy of the
   static depth) and skip cube faces without casters.
3. Composite: split the glossy reflection filter and the probe march into separate passes at
   reflection resolution.
4. Clouds: temporal reprojection (march a quarter of the cells per frame).
5. Probe faces: depth-only prepass reuse and fewer local lights per face.
6. The pre-existing mismatch of `RHI.Vulkan.Smoke.ClusteredForwardPlusMatchesTheCpuReference`
   (fails since before this phase) should be understood before more lighting changes.
