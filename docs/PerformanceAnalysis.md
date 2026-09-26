# Performance analysis (static, 2026-09-26)

A code-level review of the runtime frame before desktop profiling on the RTX 4070. Reported baseline: about 200 FPS in the sandbox (Release, windowed, vsync off), after the depth prepass, the cluster fixes and the smaller light ranges. The target is several hundred FPS, so a frame must fit in roughly 2–3 ms of CPU time and 2–3 ms of GPU time, overlapped.

Items are ordered by expected impact. "Done" items changed in this revision; the others are the plan.

## 1. CPU and GPU frames run back to back (largest structural limit)

`RenderGraphExecutor` keeps one submission in flight (its documented contract). Until this revision, `FrameRenderer::BeginFrame` waited for the previous frame's GPU work *before* the engine updated UI, gameplay, physics and render extraction. The frame time was therefore about CPU + GPU, not max(CPU, GPU).

- **Done:** the wait moved into `Render` (`GatherTimings`, right before the swapchain acquire). All of BeginFrame's collection and residency work only checks completion values, so the next frame's game update now overlaps the previous frame's GPU work.
- **Still serialized:** graph building, compilation and command recording happen after the wait, so the GPU idles while the CPU records. The fix is two executors used alternately (two frames in flight):
  - every executor owns its command pool, staging arena, query pool and transient-resource pool (transient memory doubles: about 150–250 MB at 1440p);
  - the swapchain needs one acquire semaphore per frame in flight (`RenderDevice::acquired` is reused today, which is only safe with one frame in flight);
  - each submission waits on the other executor's last timeline value, so a later completion still implies every earlier one. Retirement (`GetLastCompletion`) and the swapchain rebuild's "safe after" point keep their meaning;
  - readbacks (captures) and timestamp reads must use the executor that produced them.
  Expected gain: up to the full recording time (typically 1–3 ms of CPU per frame at this pass count).

## 2. The render graph is rebuilt and compiled every frame

About 70 passes are declared, compiled (lifetimes, barriers, aliasing) and recorded each frame. Each pass creates a descriptor table and texture views through the RHI (17 call sites in `Renderer/`).

- Cache the compiled graph and reuse it while the frame's topology (settings, page slots, feature list, resolution) is unchanged; only the upload payloads change.
- Pool descriptor tables per pass and rewrite them instead of allocating (or make the static bindings of persistent resources live in long-lived tables).
- Measure first: time `graph.Compile()` and `executor.Execute()` on the CPU (the diagnostics overlay shows CPU ms for the whole Render call).

## 3. G-buffer bandwidth of the opaque pass

The opaque pass writes seven targets per pixel: color, indirect, reflectance and specular (RGBA16F each), normal + roughness (RGBA16F), object id (R32F) and velocity (RG16F) — 48 bytes per shaded pixel, plus depth. At 2560×1440 that is about 177 MB written per frame before the screen-space passes read it back.

- Reflectance and specular feed only screen-space reflections, which are off by default. A `FORWARD_NO_SSR` variant (5 targets) removes 16 bytes per pixel when SSR is off.
- Pack normal + roughness into RGB10A2 (octahedral normal), and the object id into R32Uint only when picking needs it.
- Expected gain: about 0.2–0.4 ms at 1440p on a 4070.

## 4. Shadows are fully re-rendered every frame

Every frame renders three 2048² cascades, the playground spot light (512²) and the lantern's six cube faces (256²). Sponza alone is 262 K triangles per cascade that sees it. GPU culling limits casters per view, but static geometry is redrawn even when nothing moved.

- Cache static casters per cascade: render them only when the cascade's snapped origin changes, and copy the static depth into the atlas before drawing dynamic casters.
- Update the far cascade every other frame.
- Skip point-light faces whose frustum contains no casters (the visibility pass already knows).
- Expected gain: 0.5–1.5 ms in the Sponza view, depending on the cascade coverage.

## 5. Clustered lighting with thousands of lights

The per-cluster bitmasks (this revision) remove truncation, and assignment is cheap: one thread per (cluster, 32 lights). Shading cost is now exactly the lights overlapping each pixel's cluster, tested per pixel:

- 64-pixel tiles are coarse far away, where one cluster spans many meters. Try 32-pixel tiles (4× the clusters; the masks for 4,096 lights cost 32 MB at 1080p) or z-binning (Drobot 2017) to tighten the per-pixel sets.
- Lights scalarize well: the bitmask walk is wave-uniform when a wave shares a cluster. Consider `WaveActiveBitOr` over the pixel's mask words to walk the union once per wave.
- Use the cluster heatmap (Rendering tab → debug view) to see the per-cluster counts in the atrium.

## 6. Screen-space and post passes

- GTAO runs at full resolution with a 5×5 blur. Half resolution with a depth-aware upsample is the usual choice (about 0.3 ms saved at 1440p).
- Bloom (6 mips), histogram, exposure, TAA and the present copy are each small. The present pass could be skipped by writing the post output straight into the swapchain image when it supports storage usage.
- Volumetric clouds (new) cost about 48 steps × (1 + 4 shadow samples) of fBm per half-resolution pixel. Budget: 0.8–1.5 ms at 1440p. `ResolutionScale` 0.35, `Steps` 32 and `ShadowSteps` 3 halve it. Temporal accumulation (reprojecting last frame's clouds) is the proper next step.
- The lens flare recomputes its 12-tap sun visibility per pixel. It is cheap (cached loads), but moving the visibility into a 1-thread pre-pass removes the redundant work.

## 7. Startup and assets

Loading cooked assets validates the content hash of every `.sasset` on every start. In Debug builds this takes minutes for Sponza (about 350 MB of RGBA8 texture data). Release is much faster, but the work is still redundant.

- Record size and modification time next to the hashes and skip re-hashing unchanged files.
- Keep only one Sponza variant in `Assets/Models/Sponza/`. Every `.glb` there is cooked and loaded; the sandbox uses `sponza-ktx-draco.glb`.
- Transcode Basis textures to BC7 instead of RGBA8. This is four times less texture memory and bandwidth, and needs block-aware copies in the RHI and residency.

## 8. Windowed mode on Windows

With vsync off (the default) the swapchain uses mailbox, else immediate. While the window is being dragged, the Win32 modal move loop blocks the main thread, so frames stop until the drag ends. That is not a throughput problem, but it looks like one. Rendering from SDL's event watch during the move loop, or moving rendering to its own thread, would keep frames flowing.

## Profiling checklist for the 4070

1. Build Release (`build-windows-soft.bat` without `-Debug`) and run with `--validation=off`.
2. Press X for the diagnostics overlay. It shows CPU ms, GPU ms (the sum of pass timestamps) and the five costliest passes.
3. Toggle one thing at a time on the Rendering tab: clouds, shafts, flare, shadows, GTAO, TAA, bloom. Compare the costliest passes.
4. Check CPU against GPU. If CPU ms is close to the frame time, item 1 (two frames in flight) and item 2 (graph caching) come first. If GPU ms is, items 3–6 do.
5. Take an Nsight Graphics frame capture of the Sponza view (key 6) for per-pass occupancy and bandwidth.
