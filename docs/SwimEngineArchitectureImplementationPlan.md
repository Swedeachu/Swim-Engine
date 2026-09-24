# Swim Engine — Modern Cross-Platform Architecture and Implementation Plan

> **Mission:** turn Swim Engine into a clean, general-purpose, cross-platform C++ engine whose low-level platform contracts are stable before higher-level systems are built, whose main renderer is a modern GPU-driven Vulkan renderer behind an explicit RHI, and whose major replaceable systems can be selected at runtime without leaking their implementation libraries into gameplay code.
>
> **Primary targets:** Windows and Linux are first-class now. macOS/iOS and Android are later targets that the architecture must make straightforward rather than requiring another foundational rewrite.
>
> **Rendering direction:** Vulkan is the primary modern graphics backend. D3D12 and Metal are future RHI backends. OpenGL remains a functional legacy renderer, but it must not constrain the modern RHI or require feature parity with the GPU-driven renderer.
>
> **Physics direction:** gameplay talks only to Swim physics contracts. PhysX and Jolt are selectable backend implementations.
>
> **Shader direction:** Slang is the canonical source language/compiler stack for all first-party shaders. Vulkan consumes Slang-generated SPIR-V. The isolated legacy OpenGL backend consumes Slang-generated GLSL compatibility artifacts. Future D3D12 consumes DXIL. Metal support is added when the Slang/Metal path is sufficiently mature.

---

## Current implementation snapshot — 2026-09-24

This section is the short authoritative status summary for the current repository. Detailed historical checkpoints remain below because they explain why particular contracts exist, but this snapshot should be read first when deciding what to work on next.

**Runtime integration status:** checkoffs below distinguish implemented contracts from migrated consumers and completed desktop validation. `Swim Engine.exe` still constructs the transitional `Engine::VulkanRenderer`; `--graphics=vulkan` does not select the new `Swim::RhiVulkan` path. Item **39** is now validated by the separate native RHI suites; launching the sandbox does not exercise that path. The build scripts run default `SwimTests` cases; real-driver RHI smokes require `SWIM_RUN_RHI_SMOKE=1` and the documented validation profiles.

| Area | Sandbox integration today |
| --- | --- |
| Platform/Input/configuration | Active: SDL windows/events, normalized InputSystem, one accepted-frame input snapshot, explicit engine-owned services and runtime configuration. |
| Jobs/memory | Active: enkiTS jobs in renderer/BVH work, mimalloc, job scratch scopes and CPU frame-arena lifecycle. CPU frame memory is separate from RHI GPU frame contexts. |
| Scene/physics | Active: catalog/runtime identities, scene command buffer, scene-owned transforms, per-view frustum, generic physics handles/bridge. PhysX is default; Jolt is selected explicitly when compiled. `RenderExtractor` (EnTT `MeshRenderer` → GPU Scene, item 47) exists but no scene constructs it yet. |
| Animation | `Systems/Animation` (item 78): skeletons, clip sampling, pose blending, layered state machines, events, root motion, sockets and skinning palettes, jobified. Cooked skeletons and clips load through `.sasset`; no scene constructs an animator yet (item 56). |
| Assets | Active cooked `.sasset` identity/load path and development auto-cook, with legacy engine-owned pools and `LegacyRenderBinding` still adapting assets for rendering. Async IO is available/injected. `Renderer/Residency` now streams cooked meshes/textures through async IO and job decodes into `GeometryHeap`/`TextureResidency` (item 44), but the engine runtime does not construct it yet. |
| Text/UI | `Systems/Text` font faces/shaping/MSDF atlas and `Systems/UI` retained layout/paint/input foundation (item 79) are available to consumers and tested. No new RHI UI pass or sandbox wiring yet; legacy text/UI remains active. |
| Shaders | Active Slang-generated shaders for the transitional renderer. Reflected modern RHI program/layout binding remains a separate consumer. |
| Commands/editor | Active command registry. Retired input managers, external-editor IPC and scene-JSON experiment have no active runtime references. Durable scene entity identities remain active. |
| New Vulkan RHI | Separate tests/consumers: devices/resources, timelines, swapchains/HDR, transfers/arenas, draw/compute pipelines, typed descriptors and diagnostics. It does not yet render the sandbox. |
| Modern renderer | RenderGraph DAG/state/barrier compilation, single-queue execution and executor-staged upload/readback transfers are implemented with native reference consumers. Generational GPU registries, the paged GeometryHeap with submeshes, TextureResidency, the asynchronous `AssetResidencyService`, the bindless texture/sampler table with sampler residency, the persistent `GpuScene` with dirty-only record uploads and GPU-driven visibility (frustum culling, reverse-Z HZB and two-phase occlusion culling, LOD hysteresis, bounded binning, compaction and `DrawIndexedIndirectCount` command generation; items 42–46, 48–55, 57), material templates with the GPU material table and metallic-roughness PBR (items 58–60), GPU-built image-based lighting with a PBR regression gallery (items 61–62), the GPU light buffer (item 63), GPU clustered light assignment with overflow diagnostics (items 64, 65, 68), opaque/transparent Clustered Forward+ with light-count benchmarks (items 66, 67, 69), cascaded/spot/point shadows in one atlas sampled by Forward+ (items 70–72) the post stack (auto exposure, bloom, grading, tone mapping to sRGB/HDR10/scRGB; items 73–74) temporal anti-aliasing with Forward+ motion vectors and jitter (item 75), screen-space GTAO, reflections and exponential height fog (item 76), GPU particles (item 77) and compute GPU skinning with morph targets and deformation motion vectors (item 78) exist as backend-neutral layers with CPU/mock coverage and opt-in native smokes; nothing in the sandbox consumes them yet (item 56). |

- **Latest checkpoint — item 79, text and retained UI foundation (2026-09-24):** the first runtime portion of Phase 20 is implemented; item 79 remains open until rendering and the remaining text/widget capabilities are delivered.
  - `Systems/Text/FontFace` owns font bytes and FreeType/HarfBuzz resources. It shapes horizontal UTF-8 runs with byte clusters, language/script/direction options, kerning and ligatures, metrics, missing-glyph counts and immutable concurrent shaping. Arabic contextual shaping/RTL and four-byte code points are covered by a bundled, licensed font fixture.
  - `GlyphAtlas` converts FreeType line/quadratic/cubic outlines into top-down RGB8 MSDF pages. Fixed page/byte budgets, cache hits, stable placements, padding, page revisions and retained font ownership provide the CPU side of a future upload consumer. Spaces consume no texels; oversized/full-atlas requests fail without changing existing entries. Generation is synchronous; prewarm glyphs before time-critical rendering.
  - `Systems/UI/UiDocument` owns a retained hierarchy with unique node IDs, cycle/depth checks, intrinsic/preferred/percentage/min/max sizing, row/column/overlay flow, margins/padding/gaps, absolute offsets, logical DPI units, clipped scrolling and ordered solid/glyph paint quads. Paint colors are premultiplied linear RGBA. It has pointer hit-testing/capture/cancel, focus traversal, keyboard activation and queued events; removal/disable/hide invalidates focus and capture safely.
  - Layout skips unchanged documents; unchanged text assignments reuse shaped runs. A dirty layout currently recomputes the whole document and paint output is rebuilt on demand. Hard lines support LF/CRLF; paragraph bidi, fallback, wrapping, caret/selection and IME are not implemented. This is a UI foundation, not an editable widget toolkit.
  - Build wiring compiles the modules into `SwimEngine` and dependency-enabled `SwimTests`. Text libraries remain private; `SwimTextUiPublicHeaders` and `verify-build-layout.py` enforce public and subsystem boundaries. No sandbox scene constructs the new document and no RHI UI pass exists yet; legacy text/UI remains active until explicit migration.
  - **Validation:** 20 focused cases / 1,932 checks pass with the exact pinned dependencies in a standalone Linux build of the repository's new module and test sources (17 new cases plus the 3 dependency cases). The same cases pass with AddressSanitizer/UndefinedBehaviorSanitizer on first-party sources; LeakSanitizer is unavailable under this environment's process tracing. The repository layout verifier and actual CMake public-header target pass. Full engine/default-suite, MSVC and desktop validation were not run here.
  - **Next:** add the RenderGraph UI renderer/atlas uploads and desktop smoke; then paragraph segmentation/fallback/wrapping and editable widgets. Keep item 79 open. Detailed contracts, reproduction commands and commit text are in [Text and retained UI](TextAndUi.md).
- **Previous setup — item 79 text libraries (2026-09-24):** only the dependencies of Phase 20; no UI or text code yet.
  - `cmake/TextDependencies.cmake`, included from `CMakeLists.txt` after the platform dependencies, pins:
    - FreeType `VER-2-14-3`, with zlib, bzip2, PNG, Brotli and HarfBuzz disabled;
    - HarfBuzz `14.5.0`, built as its documented single translation unit (`src/harfbuzz.cc`, target `SwimHarfBuzz`). Its own CMake build is community-maintained and warns on every configure;
    - msdfgen `v1.13`, core only (no vcpkg, Skia, PNG or tinyxml), following Swim's `CMAKE_MSVC_RUNTIME_LIBRARY` instead of its own Debug CRT. Its CMakeLists also sets the global `PREDEFINED_TARGETS_FOLDER` to `meta`, so the first Windows clean build failed the solution-layout check (the `CMake` folder was missing). `TextDependencies.cmake` now restores `CMake` after adding msdfgen, and the layout script requires that line.
  - They are bundled as `Swim::TextDependencies`. Only `SwimTests` links it for now, through the new `Text` suite group. `Text.Dependencies` (3 cases) initializes FreeType, shapes UTF-8 with HarfBuzz (a four-byte code point, Arabic script/RTL detection) and renders a square MSDF.
  - `verify-build-layout.py` pins the three tags and keeps their headers inside `Systems/Text`, `Systems/UI` and `Tests/Suites/Text`; each rule was checked to fire on a planted violation.
  - The Linux configuration passes **677 cases / 652,871 checks** (was 674).
  - **Windows note:** the soft build configures fully disconnected and cannot fetch new packages. Configure once with downloads enabled (for example `cmake --preset windows-release -DFETCHCONTENT_FULLY_DISCONNECTED=OFF`) or run the clean build. After that, soft builds reuse the cache.
  - **Followed by:** the text and retained UI foundation checkpoint above.
- **Previous checkpoint — item 78: animation, skinning and morph targets (2026-09-24):** Phase 18, end to end from glTF to motion vectors.
  - **Assets:**
    - The glTF importer now reads skins (joints, inverse binds, `JOINTS_0`/`WEIGHTS_0`, Draco included), morph targets (position/normal/tangent deltas, mesh default weights) and every node-targeted animation channel (translation, rotation, scale, morph weights; step, linear, cubic spline).
    - The static model compiler emits `Skeleton` (7) and `AnimationClip` (8) `.sasset`s. Joints are ordered parents first with remapped vertex joints; influences are normalized, sorted and stored in a second 24-byte vertex stream; morph targets are dense per vertex. Clips bind by unique joint/node names. Models reference their skeletons, clips, per-node skins and morph weights.
    - Static meshes and models keep payload version 1 byte for byte; version 2 carries the new data, and readers validate all of it. The compiler profile is `v2`, so auto-cook recooks once.
    - Known limitation: the pinned fastgltf 0.9 rejects node-level `weights` arrays (an inverted check in its node parser); mesh-level weights work.
  - **`Systems/Animation` (CPU):**
    - `Skeleton`, `AnimationClip` (binding by name), `SampleClip`, `BlendPose`, `MakeAdditivePose`/`AddPose` and `BoneMask`.
    - `Animator`: layered override/additive state machines with float/bool/trigger parameters, any-state and exit-time transitions, linear crossfades, `Play`, events forward/backward/across loops, root motion from layer 0 (per axis, accumulated across loops), playback speed/direction/looping and morph weight overrides.
    - `SkeletonInstance`: model transforms, the skinning palette (`model × inverseBind`, GPU 3×4 rows) with the previous palette and weights, and sockets. `UpdateAnimations` runs animators and instances over `JobSystem::ParallelFor`, identical to the serial result.
    - It depends only on Jobs and the asset structs.
  - **`Renderer/Skinning` (GPU, `SwimSkinning`):**
    - Compute linear blend skinning with sparse per-vertex morph deltas, once per character per frame, into a `GeometryHeap` output mesh of 2 × vertices. The first half holds the skinned `StandardVertex`s, the second the previous-frame positions.
    - Visibility, shadows and Forward+ draw it as any mesh. Instances settle once (previous = current) after their last `SetPose`. `ComputeBounds` is a conservative per-joint-box bound, morph reach included.
    - `GpuInstanceRecord::Reserved` became `PreviousVertexOffset`. Clustered Forward+ reads the previous local position through it in the same load loop, 73 of 75 GPU-AV accesses. `ForwardPlus::MotionVector` gained the previous-local overload.
  - **Validation:**
    - The official Linux configuration passes **674 cases / 652,846 checks** (was 648 / 641,003). The EnTT-enabled sanitizer build passes 697 cases cleanly. See [the item 78 record](validation/Item78-2026-09-24.md).
    - The new native smoke `GpuSkinningMatchesTheCpuReference` compares 4,080 skinned vertices and their previous positions over 10 frames of three animated characters with 0 outliers (worst 2.4·10⁻⁷). It also checks settling and pose bounds, and passes all four validation profiles on Mesa lavapipe.
    - The Forward+ smoke now draws its gold cube with shifted previous positions from the moved frame on: 1,431 and 1,672 deformed pixels, 0 velocity outliers under core and synchronization validation. GPU-AV on lavapipe still hits only the layer-1.4.350 reports already recorded.
    - 41 native cases now. See [Animation and skinning](Animation.md).
  - **Still needed:** ~~desktop execution of the new and changed smokes on the RTX 4070 (all four profiles), then recording the skinning timing as a budget~~ *(done 2026-09-24: 41/41 native cases pass all four profiles on the RTX 4070 and the Windows default suite passes 714 cases / 656,920 checks. Skinning 64 × 1,032 vertices takes 0.054–0.074 ms (core/sync); see [the item 78 record](validation/Item78-2026-09-24.md#desktop-run-rtx-4070))*. Engine wiring (ECS animator/skinned-mesh components, residency of skeletons and clips) remains item 56. Runtime UI and text (item 79) is next; its libraries are in place (below).
- **Previous renderer checkpoint — item 76 complete (screen-space reflections) and item 77, GPU particles (2026-09-24):** the last screen-space module and Phase 19's GPU particle system.
  - **Forward+ reflectance and specular targets (item 76):**
    - A reflection found on screen must *replace* the specular IBL already inside Color, and a colored metal must tint it. The opaque pass therefore writes `ForwardPlusTargets::Reflectance` (the split-sum specular reflectance, `StandardPbr::EnvironmentSpecularWeight`) and `ForwardPlusTargets::Specular` (the specular IBL, `Prefiltered ×` reflectance), both RGBA16Float and scaled behind glass like Indirect. `ForwardPlus::SpecularEnvironment` is the CPU definition.
    - Seven opaque and four transparent color attachments; every desktop Vulkan driver exposes 8. Packing the thin G-buffer is a later bandwidth optimization.
    - A `BrdfLut` may now be supplied without an environment (`ForwardViewFlagBrdfLut`), so reflections work without IBL. Karis' analytic split-sum fit was tried as a LUT-free fallback and rejected: against this engine's height-correlated LUT it is off by up to 0.4.
  - **Screen-space reflections (`SwimScreenSpaceReflection`, item 76):**
    - One mirror ray per pixel, marched in screen space with perspective-correct depth (after McGuire and Mara 2014): clipped to the near plane, jittered per pixel and frame for TAA, a thickness hit test, binary refinement, back-face rejection, and a confidence from roughness, screen-edge and distance fades. The hit's color carries its own AO.
    - The composite replaces `confidence × ao × specular` with `confidence × ao × reflectance × hit`, so misses and disabled reflections leave the IBL untouched. `GpuScreenSpaceParams` grows to 400 bytes (projection + reflection fields); one 1×1 stand-in fills the three reflection slots when reflections are off. `ScreenSpace::TraceReflection`, `ReflectionTexel` and the extended `CompositeTexel` are the CPU definitions.
    - Glossy (importance-sampled) rays, hierarchical-Z tracing and reflecting the previous frame's final image remain later work.
  - **`Renderer/Particles` (item 77):**
    - `ParticleSystem` keeps a persistent pool with per-emitter ranges, free lists, draw lists, counters and indirect arguments. Emitters are generational handles whose ranges retire after their last GPU use and coalesce.
    - `Simulate` records four compute passes: simulate (semi-implicit Euler, drag, ground-plane bounces; dead slots return to the free list), emit (spawns pop free slots; drops are counted), compact (live slots onto the draw list) and finalize (a one-group bitonic back-to-front sort for alpha-blended emitters, then each emitter's `DrawIndexedIndirectCommand`).
    - `Draw` records one pass of camera-facing billboards: additive emitters first, then alpha-blended ones back to front by origin, each one indirect draw. Size and color curves over life, flipbooks over life or by frame rate, and bindless sprites (or a procedural disc) shared with Forward+'s table.
    - Emission is the only CPU work: `Particles::AdvanceEmission` (rate, bursts, looping cycles, capacity cap). Every random draw is a pure function of the particle id and the emitter seed, so the GPU and `Particles::` agree particle by particle whatever slot each lands in.
    - The module depends only on RenderGraph, the RHI contract and `Resources/` handles; `scripts/verify-build-layout.py` enforces it.
  - **Validation:**
    - The official Linux configuration passes **648 cases / 641,003 checks** (was 626 / 374,352). The EnTT-enabled sanitizer build passes 671 cases cleanly. See [the item 76 record](validation/Item76-2026-09-24.md) and [the item 77 record](validation/Item77-2026-09-24.md).
    - The native smokes now run in the container on **Mesa lavapipe** (llvmpipe, SDL's offscreen video driver) with a source-built Khronos validation layer 1.4.350:
      - `ScreenSpaceEffectsMatchTheCpuReference` (with two new reflection frames: GPU and CPU agree on every hit, 0 outliers), the extended `ClusteredForwardPlusMatchesTheCpuReference` (0 reflectance/specular outliers), item 75's `TemporalAntiAliasingMatchesTheCpuReference` and the new `GpuParticlesMatchTheCpuReference` pass core and synchronization validation;
      - the particle, screen-space and TAA smokes also pass GPU-assisted and combined validation. The Forward+ smoke fails those two profiles on `SharedMemoryDataRace-RaceOnStore` in item 61's `EnvironmentIrradiance.slang` and item 65's `ClusterScan.slang` tree reductions. Both read as race-free (a workgroup barrier between rounds, correct barrier semantics in the SPIR-V, each report an invocation against itself) and a 16-byte element stride did not change the report, so they are recorded as an open question about this newest layer's detector, not changed; the desktop SDK passed these profiles before.
      - The whole native suite (40 cases): 38 pass core and synchronization validation (the two window minimize/restore smokes fail because the offscreen driver cannot minimize), 31 pass GPU-assisted and combined (the same two, plus seven earlier smokes on the reductions above and on a `WARNING-GPU-AV-drawCount` in item 49's visibility smoke, whose per-bin counts deliberately exceed the capacity that `maxDrawCount` clamps). See [the item 77 record](validation/Item77-2026-09-24.md#whole-native-suite-on-mesa-lavapipe).
    - `GpuParticlesMatchTheCpuReference` compares 4,732 particle-steps over 30 frames with 0 id mismatches and 0 field outliers (worst 10⁻⁷), 891 sorted pairs with 0 inversions, and 36,427 drawn pixels with 0 outliers (532 occluded by the depth plane).
    - 40 native cases now. See [Screen-space effects](ScreenSpace.md), [GPU particles](Particles.md) and [Clustered Forward+](ForwardPlus.md).
  - **Desktop run (RTX 4070 Laptop, NVIDIA 581.29, layer 1.4.357, 2026-09-24):** the default suite (688 cases / 645,077 checks) and all 40 native cases pass core, synchronization, GPU-assisted and combined validation with 0 warnings (user report). Reflections: 10,027 / 10,027 and 9,985 / 9,985 hits, 0 outliers; Forward+ surface targets 0 outliers in every frame; particles 0 mismatches (worst 1.14·10⁻⁶), 0 inversions, 0 pixel outliers. The lavapipe/1.4.350 GPU-AV reports (shared-memory race on the irradiance and cluster-scan reductions, item 49's drawCount warning) did **not** reproduce there, so they are recorded as that layer's behaviour on lavapipe and nothing was changed.
  - **Budgets (core, 1080p unless stated):** GTAO 1.28 ms, blur 0.42 ms, reflections 1.08 ms, screen-space composite 0.29 ms; TAA resolve 0.26–0.33 ms; 60k particles: simulate 0.027, emit 0.008, compact 0.005, finalize 0.004, draw 0.157 ms. Recorded in the [item 75](validation/Item75-2026-09-24.md), [item 76](validation/Item76-2026-09-24.md#desktop-run) and [item 77](validation/Item77-2026-09-24.md#desktop-run) records, with the post, clustering and shadow budgets in their own records. Animation/skinning (item 78) followed; engine wiring remains item 56.
- **Previous renderer checkpoint — item 76, first part: screen-space ambient occlusion and height fog (2026-09-24):** Phase 17's optional screen-space modules start, between Forward+ and TAA. SSR, the third module of item 76, was left for the latest checkpoint.
  - **Forward+ surface targets:**
    - The opaque pass writes two more attachments. `ForwardPlusTargets::Normal` (RGBA16Float) holds the world shading normal after normal mapping, with perceptual roughness in w. `ForwardPlusTargets::Indirect` (RGBA16Float) holds the ambient + IBL radiance (`ForwardPlus::IndirectRadiance`) already inside Color. Transient targets stand in when none are supplied.
    - The transparent pass writes (0, 0, 0, alpha) to the indirect target with the same premultiplied blend, so behind glass it holds exactly the transmitted share. The heatmap view writes 0.
    - Ambient occlusion can therefore darken indirect light only. Direct light, shadows and emission stay untouched, which a post-hoc AO over the whole color cannot do in a forward renderer.
    - Five opaque color attachments; Vulkan desktop drivers expose at least 8.
  - **`Renderer/ScreenSpace`:**
    - **GTAO:** horizon-based, cosine-weighted visibility from reverse-Z depth and the Forward+ normals, after Jimenez et al. It samples 1–4 slices per pixel along noise-rotated screen directions (interleaved gradient noise, rotated per frame for TAA) and 1–8 pixel-snapped steps per side. The world-space radius is capped in pixels; occluders fade out over `Falloff` × radius; horizons are clamped to the normal's hemisphere. Each slice integrates the visible arc weighted by the projected normal length.
    - **Blur:** a 5×5 depth-aware blur whose weights fall to 0 at a relative view-depth difference of `BlurDepthTolerance`. It clamps and applies `Power` only after averaging, so single-slice estimates above 1 on open surfaces are not biased down.
    - **Composite:** `color − (1 − ao) × indirect`, then analytic exponential height fog: closed-form optical depth from a start distance with height falloff, `Color` plus a Henyey-Greenstein sun lobe (normalized to 1 when isotropic), and the sky fogged at `MaxDistance`.
    - All three programs reconstruct positions through the inverse unjittered projection with the frame's jitter removed. AO off records only the composite with a 1×1 stand-in; AO and fog off records nothing. The module depends only on RenderGraph and the RHI contract; `ScreenSpace::` in `ScreenSpaceReference` is the CPU definition.
  - **Validation:**
    - The official Linux configuration passes **626 cases / 374,352 checks** (was 616 / 276,507). The EnTT-enabled sanitizer build passes 649 cases cleanly.
    - CPU tests over ray-cast scenes: open floors and walls stay above 0.97 on average; the floor at a wall's base drops to about 0.62, and occlusion fades with distance and radius. The blur keeps constants and does not cross depth edges. Fog matches numeric integration of its density to 10⁻⁴, and the phase function integrates to 4π.
    - The Forward+ smoke now also compares the normal + roughness and indirect targets with the CPU ray cast, including the transmittance behind glass.
    - The new native smoke `ScreenSpaceEffectsMatchTheCpuReference` compares every stage texel by texel, each from the GPU's own previous stage: raw GTAO, the blur, and the composite. It covers AO alone, AO with jittered fog and R32Float depth, fog alone, all off, and a 1080p timing frame.
    - `ShaderCompiler.GpuAvBudget` covers the three programs (AO 12, blur 6, composite 16 instrumented accesses).
    - 39 native cases now. See [Screen-space effects](ScreenSpace.md) and [the item 76 record](validation/Item76-2026-09-24.md).
  - **Still needed:** ~~desktop execution of the new and changed smokes (and item 75's), then recording AO and fog timings as budgets~~ *(done 2026-09-24: all four profiles pass on the RTX 4070; budgets in the item 76 record)*. SSR, the rest of item 76, followed. Engine wiring remains item 56.
- **Earlier renderer checkpoint — item 75: motion vectors and temporal anti-aliasing (2026-09-24):** Phase 17's post stack gains TAA, fed by motion vectors that Forward+ now writes.
  - **Motion vectors and jitter (Forward+):**
    - `ForwardViewRecord` grows to 208 bytes: `ViewProjection` stays unjittered, and `PreviousViewProjection` and `Jitter` (an NDC offset) follow. The previous matrix defaults to the current one, so a first frame has no camera motion.
    - The vertex stage adds `Jitter × w` to the clip position only. It also passes the unjittered current and previous clip positions, the latter from `GpuTransformRecord::Previous`, which the GPU Scene already kept.
    - The opaque pass writes a third attachment, `ForwardPlusTargets::Velocity` (RG16Float, UV now minus UV before, y down), or a transient one when none is supplied. `ForwardPlus::MotionVector` is the CPU definition.
    - Row and projection copies go through loops, so the opaque program stays at 72 instrumented buffer accesses (GPU-AV budget 75).
  - **`Renderer/Temporal` (TAA):**
    - `TemporalAntiAliasing` supplies a Halton(2, 3) jitter sequence (8 phases by default) and records one compute resolve per frame into two persistent RGBA16Float textures that ping-pong as output and history.
    - The resolve takes the velocity of the nearest texel in the 3×3 neighborhood (reverse-Z depth), reprojects the history with a clamp-to-edge bilinear filter, and clips it toward the centre of a YCoCg variance box intersected with the neighborhood's min/max. It then blends with `Feedback` using luminance weights 1/(1+L).
    - The current frame alone is used on the first frame, after `ResetHistory` (camera cuts), after a resize, and wherever the reprojection leaves the screen. NaN, infinite and negative inputs are sanitized.
    - A resize hands the old textures to the next graph to release after GPU completion. The module depends only on RenderGraph and the RHI contract; `Temporal::` in `TemporalReference` is the CPU definition.
  - **Validation:**
    - The official Linux configuration passes **616 cases / 276,507 checks** (was 606 / 275,973). The EnTT-enabled sanitizer build passes 639 cases cleanly.
    - CPU tests show that a jittered static edge converges to a stable anti-aliased value while the unjittered one stays aliased, and that dilated motion vectors with clipping leave no ghost trail where a loose box without them does.
    - The Forward+ smoke now compares the velocity target with `ForwardPlus::MotionVector` for every interior opaque pixel. Its moved frame is jittered by (0.37, −0.21) pixels, and the CPU ray cast follows the jitter.
    - The new native smoke `TemporalAntiAliasingMatchesTheCpuReference` resolves jittered, panning HDR frames with a moving rectangle, and compares every texel with `Temporal::ResolveTexel` over the GPU's own previous output. It covers the first frame, history, a reset, a resize to R32Float depth, and 1080p timing.
    - 38 native cases now. See [Temporal anti-aliasing](TemporalAntiAliasing.md) and [the item 75 record](validation/Item75-2026-09-24.md).
  - **Still needed:** ~~desktop execution of the new and changed smokes, then recording TAA timings as budgets~~ *(done 2026-09-24: all four profiles pass on the RTX 4070; resolve 0.26–0.33 ms at 1080p)*. AO and fog (item 76) followed.
- **Earlier renderer checkpoint — items 73 and 74: exposure, tone mapping, bloom and color grading (2026-09-23):** Phase 17's post stack starts: HDR scene color now becomes display output entirely on the GPU.
  - **`Renderer/PostProcess` (item 73):**
    - A 256-bin log-luminance histogram (group-shared atomics) and a one-thread exposure pass.
    - The exposure pass averages between percentiles, meters with EV100, clamps, applies compensation and adapts asymmetrically over time. It works in a persistent `GpuExposureState`, with snaps on the first frame and after `ResetExposureHistory`. Manual EV skips the histogram.
    - Four tone mappers: Clamp, Reinhard with a white point, ACES (Hill fit) and Khronos PBR Neutral (the default).
    - Three encodings: sRGB with interleaved-gradient dither into RGBA8Unorm, and HDR10 (BT.2020 + PQ) or scRGB into RGBA16Float. HDR tone-maps relative to the display peak, with SDR white at paper white.
  - **Item 74:**
    - Bloom: a Karis-averaged, soft-thresholded 13-tap downsample chain whose taps are exact 2×2 box averages, and a tent upsample chain that accumulates.
    - Grading: white balance (LMS von Kries toward the daylight locus), contrast around 18 % grey, ASC CDL and saturation. Default values skip grading entirely.
  - Every pass has a CPU definition in `PostProcessReference` (`Post::`), and every effect can be disabled cleanly. The module depends only on RenderGraph and the RHI contract.
  - Compute shaders cannot store to sRGB formats, so the sRGB OETF is applied in the shader.
  - **Validation:**
    - The official Linux configuration passes **606 cases / 275,973 checks** (was 591 / 260,063). The EnTT-enabled sanitizer build passes 629 cases cleanly.
    - The new native smoke `PostProcessMatchesTheCpuReference` reads back the histogram, the exposure state, every bloom level and every output texel, and compares each with the CPU definition. It runs five compared frames (auto sRGB, adaptation, graded ACES, HDR10, scRGB) plus a 1080p timing frame.
    - `ShaderCompiler.GpuAvBudget` covers the six post programs.
    - 37 native cases now. See [Post-processing](PostProcess.md) and [the items 73–74 record](validation/Items73-74-2026-09-23.md).
  - **Desktop run (RTX 4070 Laptop, 2026-09-24):** the default suite and all 37 native cases pass the four validation profiles (user report). *(Budgets recorded 2026-09-24: histogram 0.20, exposure 0.02, bloom 0.51, composite 0.17 ms at 1080p.)* Motion vectors and TAA (item 75) followed.
- **Earlier renderer checkpoint — items 70, 71 and 72: directional, spot and point shadows (2026-09-23):** Phase 16's first checkpoint shadows every light kind from one depth atlas, rendered GPU-driven and sampled by Clustered Forward+.
  - **`Renderer/Shadows`:**
    - **Directional (item 70):** `ComputeCascades` fits 1–4 cascades (practical splits) with rotation-invariant bounding spheres, texel-snapped centers and a caster extension toward the light.
    - **Spot (item 71):** each spot gets one cone view. `ShadowAtlasAllocator` places power-of-two tiles in priority order and keeps last frame's tiles for unchanged requests. When the atlas is full it downgrades, then displaces lower-priority tiles, then evicts; downgraded tiles grow back when room frees.
    - **Point (item 72):** six cube faces as atlas tiles, only for lights with `CastsShadows`. Cost is controlled by per-kind budgets (`MaxPointShadows` 2, `MaxSpotShadows` 16, `MaxDirectionalShadows` 1), per-light resolutions and the atlas policy.
    - `PlanShadows` turns casters into `GpuShadowRecord`s (slot = `GpuLightRecord::ShadowIndex`), `GpuShadowView`s, tiles and visibility views. Over-budget and evicted lights become `ShadowKind::None` (lit).
    - `ShadowRenderer` records one GPU caster cull per view (`GpuViewFlags::ShadowCasters`: `RenderObjectFlags::CastShadows` joins the drawable mask), then a single depth pass that draws each tile's Opaque and Masked bins. `SwimShadowMasked` is the alpha-tested variant; blended materials cast nothing.
    - Sampling is PCF with receiver-side normal-offset and slope bias, scaled by the kernel radius, using exact `Load`s. `Shadows::ShadowFactor` is the CPU definition.
  - **Integration:** Forward+ binds the atlas, records and views (15–17), sets `ForwardViewFlagShadows` and scales every shadowed directional and clustered light by its factor; without shadows it binds stand-ins. `Rhi::GetTransferTexelBytes` lets `D32Float` textures be read back (depth aspect), so the atlas can be verified.
  - **Fixed along the way:**
    - The opaque depth variant's fragment stage originally took an input struct holding only `SV_Position`. That reflects without a varying binding, which the RHI interface conversion rejects. The fragment stage now has no parameters, and `ShaderCompiler.ShadowLayout` pins it.
    - The mock command list now records pipeline, table, viewport, scissor and index-buffer binds.
  - **Validation:**
    - The official Linux configuration passes **590 cases / 260,048 checks** (was 555 / 249,085). The EnTT-enabled sanitizer build passes 613 cases cleanly.
    - The CPU sampling tests compare shadow factors with exact ray-cast visibility for cascades, spots and every cube face. They also show no acne on lit caster faces.
    - The new native smoke `ShadowedForwardPlusMatchesTheCpuReference` reads the atlas back and compares it texel by texel with CPU shadow maps: masked-away, blended and non-casting objects must be absent. It compares the shadowed image with `ForwardPlus::Shade` over the GPU's own atlas, and runs an eviction frame.
    - 36 native cases now. See [Shadows](Shadows.md) and [the items 70–72 record](validation/Items70-72-2026-09-23.md).
  - **Desktop run (RTX 4070 Laptop):** core and synchronization validation pass all 36 native cases. The shadow atlas matched the CPU shadow maps with 0 mismatches, and the shadowed image had 0 outliers (mean error 3.4·10⁻⁴). GPU-assisted validation failed both Forward+ smokes on `GPUAV-Compile-time-general-buffer`: more than 75 instrumented buffer accesses per module. The shader now has one light loop (the shadow lookup is inlined once), member view loads and single copies of the transform and vertex, which brings it from 125 to 72. `ShaderCompiler.GpuAvBudget` (591 cases now) fails any renderer program above the limit.
  - **Re-run:** after the GPU-AV fix, all four validation profiles pass. *(Budgets recorded 2026-09-24: caster culls 0.17 ms, depth 0.05 ms for 10 views, shadowed Forward+ opaque 0.08 ms.)* The post stack (items 73–74) followed.
- **Earlier renderer checkpoint — items 66, 67 and 69: Clustered Forward+ and light-count benchmarks (2026-09-23):** the modern renderer now draws and lights GPU Scene objects end to end on the GPU.
  - **`Renderer/ForwardPlus` (items 66–67):**
    - `ForwardPlusRenderer::Record` draws GPU visibility's Opaque bin GPU-driven, shaded by `ClusteredForward.slang` into HDR color, an object-id target and reverse-Z depth. Shading is `StandardPbr` through the material table and bindless textures, plus directional lights, the pixel's cluster list, ambient, optional IBL and emission.
    - It then sorts the Transparent bin (`StandardPbr::FlagAlphaBlend`) back to front on the GPU with a bitonic network. The order is deterministic, independent of compaction order, and the unused commands are zeroed. The sorted draws are blended with premultiplied alpha.
    - Meshes use `StandardVertex`, the cooked static-mesh layout (its layout id is tested against the residency layer). Normals use the cofactor transform, and mirrored instances keep correct faces and tangents.
    - `ForwardPlusReference` is the CPU definition of every rule. A cluster-heatmap debug mode is included.
  - **Item 69:**
    - The CPU scaling scenarios (0, 1k, 10k, off-screen, dense) check every `ClusterStats` field.
    - The native `ClusteredLightingScalesToTensOfThousandsOfLights` times the clustering passes at 0, 1k, 10k and 32k lights, plus 10k off-screen and 10k dense.
    - The Forward+ smoke renders 10,000 lights and prints its pass times.
  - **Validation:**
    - The official Linux configuration passes **555 cases / 249,085 checks** (was 539 / 225,498). The EnTT-enabled sanitizer build passes 578 cases cleanly.
    - The new native smoke `ClusteredForwardPlusMatchesTheCpuReference` compares every interior pixel with an exact CPU ray cast shaded by `ForwardPlus::Shade`: object ids, opaque color, composited transparency and the GPU sort order. It also covers a heatmap frame, a moved camera without an environment, and 10k lights.
    - 35 native cases now. See [Clustered Forward+](ForwardPlus.md) and [the items 66, 67, 69 record](validation/Items66-67-69-2026-09-23.md).
  - **First desktop run (RTX 4070 Laptop):**
    - The Forward+ smoke matched the CPU reference in every frame, with 0 id mismatches, 0 outliers and a mean error of 3·10⁻⁴. It failed only on one validation warning: the transparent variant wrote an unread `ObjectId` varying. That is now fixed.
    - The per-pass timings overlapped. `GraphPassTiming` gained `EndOffsetNanoseconds`, and the smokes now attribute time by end timestamps.
  - **Still needed:** a clean re-run of both smokes on all profiles, then recording the GPU timings as budgets. *(Re-run done: all profiles pass on the Windows desktop.)* Shadows (items 70–72) followed.
- **Earlier renderer checkpoint — items 64, 65 and 68: cluster grid, GPU light assignment and overflow diagnostics (2026-09-23):** local lights are now binned into compact per-cluster lists on the GPU.
  - **`Renderer/ClusteredLighting`:**
    - `ClusterGrid` (item 64): screen tiles × logarithmic depth slices, with configurable tile size, slice count, near/far and limits. It works for any perspective depth mapping, decodes view depth from the depth buffer, and is rebuilt per frame, so resizing is just a new desc.
    - `ClusteredLightAssigner` (item 65) records five deterministic compute passes with no atomics: light cull to view space, cluster AABBs, count, a one-group prefix scan into compact offsets, and write. Lists are in light-index order. Directional lights stay outside them.
    - `ClusterReference` is the CPU definition of every pass, plus `ShadeClustered`; `ClusteredLighting.slang` provides the shader-side `ClusteredShade`.
  - **Overflow and diagnostics (item 68):**
    - `MaxLightsPerCluster` truncates lists and `IndexCapacity` clamps offsets; nothing is written out of bounds, and truncation only removes light.
    - `ClusterStats` reports visible lights, requested/written/dropped indices, overflowing and non-empty clusters and the maximum raw count.
    - `RecordHeatmap` colors each pixel by its cluster's count from the depth buffer, with magenta for truncated clusters.
  - **Validation:**
    - The official Linux configuration passes **539 cases / 225,498 checks** (was 530 / 181,469). The EnTT-enabled sanitizer build passes 562 cases cleanly.
    - The CPU tests prove that the AABBs tile the volume, that assignment is conservative, and that clustered shading equals `ShadeAllLights`.
    - The new native smoke `ClusteredLightingMatchesTheCpuReference` compares every GPU output with the CPU definition over 1,502 lights in three frames: roomy, overflowing and resized after light moves.
    - 33 native cases now. See [Clustered lighting](ClusteredLighting.md) and [the items 64, 65, 68 record](validation/Items64-65-68-2026-09-23.md).
  - **Still needed:** ~~desktop execution of the new smoke~~ *(passes all profiles on the Windows desktop)*. Clustered Forward+ and the light benchmarks (items 66, 67, 69) followed.
- **Earlier renderer checkpoint — item 63: GPU light buffer (2026-09-23):** Phase 15 starts with a persistent, GPU-resident light schema.
  - **`Renderer/Lights`:**
    - `LightDesc` → `EncodeLight` → a 64-byte `GpuLightRecord`, following glTF punctual semantics: directional (lux), point and spot (candela), with an inverse-square window reaching zero at `Range`, glTF spot falloff, shadow index and flags.
    - `LightMath` is the CPU definition: evaluation, `LightBoundingSphere` (tight spot bounds for clustering) and `ShadeAllLights`, the brute-force reference clustered lighting must match. `GpuLightRecords.slang` mirrors it.
  - **`GpuLightBuffer`:**
    - Rows are dense per type, directional first and local from `FirstLocalRow`, with a header buffer of counts.
    - Handles are generational. Release swap-removes; type changes move a light between ranges.
    - Uploads are batched dirty rows, plus the header when counts change, with commit/abort.
  - **Validation:**
    - The official Linux configuration passes **530 cases / 181,469 checks** (was 520). The EnTT-enabled sanitizer build passes 553 cases cleanly.
    - The new native smoke `GpuLightBufferMatchesBruteForceShading` shades 512 points over 2,002 lights with a compute probe, matches the CPU reference, and proves churn uploads only touched rows.
    - 32 native cases now. See [GPU lights](Lights.md) and [the item 63 record](validation/Item63-2026-09-23.md).
  - **Still needed:** ~~desktop execution of the new smoke~~ *(passes all profiles on the Windows desktop)*. The cluster grid, light assignment and heatmap (items 64, 65, 68) followed.
- **Earlier renderer checkpoint — items 61 and 62: environment/IBL and the PBR image-regression gallery (2026-09-23):** the standard material now receives image-based lighting built entirely on the GPU, and a gallery pins the result per pixel.
  - **`Renderer/Environment` (item 61):**
    - `EnvironmentBuilder` records graph-scheduled compute passes:
      - a procedural HDR sky (`ProceduralSky`) or any `RGBA16Float` source cube;
      - box-filtered mips down to 4×4;
      - a GGX-prefiltered specular cube using filtered importance sampling (mip *m* = roughness *m*/(*M*−1));
      - order-2 SH irradiance, from one group with exact texel solid angles;
      - the split-sum BRDF LUT.
    - Caller-owned targets can replace the transient outputs.
    - `EnvironmentMath`, `CubeImage` (Vulkan-exact seamless trilinear sampling) and `EnvironmentReference` are the CPU definitions; `Shaders/Slang/Environment` mirrors them.
    - Environment intensity and Y rotation are per-view controls.
  - **Shading:**
    - `StandardPbr` now splits into `Resolve` → `EvaluateEnvironment` → `ShadeResolved` (split-sum IBL with roughness-dependent Fresnel, occlusion on both terms).
    - `Shade` is unchanged bit for bit. `EnvironmentLighting.slang` performs the shader-side lookup.
  - **RHI:** storage textures may be cube-compatible (square, 6*n* layers); shaders write one face and mip through 2D views.
  - **Gallery (item 62):**
    - `PbrGalleryFixture.h` is a golden CPU renderer: a 6×4 sphere-impostor gallery of gold, red plastic, white dielectric and occluded copper over roughness 0..1.
    - `RhiSmoke/PbrGallery.slang` renders the same image on the GPU.
    - The expectations are CPU-tested: the furnace is exact, highlights dim monotonically, and occlusion is exact.
  - **Validation:**
    - The official Linux configuration passes **520 cases / 161,181 checks** (was 498 / 12,375). The EnTT-enabled sanitizer build passes 543 cases cleanly.
    - A new reflection test caught a `uint3` std430 padding bug in three shader records before any GPU run.
    - Two new native smokes:
      - `EnvironmentMapsMatchTheirCpuReferences` checks every GPU stage for three environments, including a furnace;
      - `PbrGalleryMatchesTheCpuReference` checks the gallery per pixel in lit, rotated and furnace frames. `SWIM_PBR_GALLERY_DUMP` writes the images.
    - 31 native cases now. See [Environment](Environment.md) and [the items 61–62 record](validation/Items61-62-2026-09-23.md).
  - **Still needed:** ~~desktop execution of the two smokes~~ *(pass all profiles on the Windows desktop)*. HDR environment *assets* and tone mapping (item 73) remain. `GpuLightBuffer` (item 63) followed.
- **Earlier renderer checkpoint — items 59 and 60: GPU material table and metallic-roughness PBR (2026-09-23):** GPU Scene objects now shade from GPU-resident materials.
  - **`Renderer/GpuMaterials/GpuMaterialTable` (item 59):**
    - One device-local row per `MaterialInstance`, in its template's layout. The row index is what `RenderObjectDesc::MaterialSet` stores; textures and samplers are `BindlessResourceTable` indices.
    - Row 0 is a permanent fallback holding the template defaults, used for unassigned, released and out-of-range indices.
    - `Import` uploads only instances whose version changed, batched into runs, with commit/abort.
    - Released rows retire after their timeline point, reset to the defaults and are reused FIFO.
  - **Standard metallic-roughness PBR (item 60):**
    - `Materials/StandardPbr.h/.cpp` is the CPU definition: Lambert diffuse, GGX, height-correlated Smith, Schlick Fresnel with F0 0.04, exact sRGB transfer functions, and shading with texture channels, tangent-space normals, occlusion on ambient, emission, alpha mask and double-sided flipping.
    - `Shaders/Slang/Materials/StandardPbr.slang` mirrors it line for line.
    - `StandardMaterial.h` provides the built-in template with glTF defaults; its hand-written layout is proven equal to the compiled shader's reflection.
  - **Validation:**
    - The official Linux configuration passes **498 cases / 12,375 checks** (was 490). The EnTT-enabled sanitizer build passes 521 cases cleanly.
    - The CPU tests prove GGX normalization, Fresnel limits, reciprocity, energy (directional albedo ≤ 1), clamping and every shading rule.
    - The new native smoke `StandardMaterialsShadeFromTheGpuMaterialTable` does three things:
      - compares the GPU BRDF with the CPU definition for 96 random inputs;
      - draws six GPU Scene quads GPU-driven with bindless textures (sRGB base color, metallic-roughness channels, a derivative-frame normal map, occlusion + emission, alpha mask, out-of-range fallback), checking every pixel against `StandardPbr::Shade`;
      - proves that material edits upload only the changed rows.
    - 29 native cases now. See [Materials](Materials.md#gpu-material-table-item-59) and [the items 59–60 record](validation/Items59-60-2026-09-23.md).
  - **Still needed:** ~~desktop execution of the new smoke~~ *(passes all profiles on the Windows desktop)*. Environment/IBL (item 61) and the PBR image gallery (item 62) followed.
- **Earlier renderer checkpoint — item 58 material templates and instances (2026-09-23):** Phase 14 starts with a backend-neutral material data layer.
  - `Renderer/Materials`:
    - `MaterialTemplate` is an immutable, shared std430 parameter layout. Types are float..float4, uint, int and bindless texture/sampler indices. Alignment, overlap, bounds and name rules are validated, and templates carry per-template defaults.
    - `MaterialInstance` is a cheap mutable record with typed setters and getters. A version counter only advances on real changes, so item 59's GPU material buffer can upload dirty records only.
  - **Reflection:** structured-element reflection now records each leaf's scalar type and component count, and `ShaderCompiler::BuildMaterialTemplateDesc` turns a shader's material struct into a template. `Shaders/Slang/Materials/StandardMaterialParameters.slang` is the metallic-roughness record that item 60 will shade with.
  - **Validation:** the official Linux configuration passes **490 cases / 11,355 checks** (was 486). The EnTT-enabled sanitizer build passes 513 cases cleanly. No new native smoke; the case count stays at 28. See [Materials](Materials.md).
- **Earlier renderer checkpoint — no-IndirectCount fallback for GPU visibility (2026-09-23):** this closes the last open Phase 13 "required behavior" box that does not depend on item 56.
  - `VisibilityDraws.h` adds `SelectVisibilityDrawPath` (the count path whenever the device has `IndirectCount`) and `DrawVisibilityBin`.
  - On devices without `IndirectCount`, the fallback zeroes the phase's command buffer (`VisibilityFrameDesc::ZeroUnusedCommands`) and issues each bin's whole capacity with `DrawIndexedIndirect`; unwritten slots draw zero instances.
  - **Validation:**
    - The official Linux configuration passes **486 cases / 11,266 checks** (was 485 / 11,248).
    - The existing native `GpuVisibilityCullsBinsAndDrawsIndirect` smoke gained a fourth frame drawn through the fallback with the same command, count, statistic and pixel checks, plus zero-instance checks for every unwritten slot. The native case count stays at 28.
    - See [GPU visibility](GpuVisibility.md#drawing-the-bins-and-the-no-count-fallback) and [the record](validation/IndirectFallback-2026-09-23.md).
- **Earlier renderer checkpoint — items 50 and 51 implemented (2026-09-23):** the depth-convention gate is closed and GPU visibility gained hierarchical-Z occlusion culling that never hides newly visible objects.
  - **Depth convention (the item 50 gate):** reverse-Z is final for the modern renderer.
    - Near → 1, far/infinity → 0, `D32Float`, clear 0, `GreaterEqual` (`DepthConvention.h`).
    - `OrthographicReverseZRowMajor` and `PerspectiveReverseZRowMajor` (infinite far) build canonical projections.
    - Views record their convention (`GpuViewFlags::ForwardDepth` for the opt-in forward mapping).
  - **`HzbBuilder` (item 50):** reduces a sampled depth texture into a transient `R32Float` pyramid (`HzbReduce.slang`), one graph compute pass per mip. Each texel keeps the farthest depth of its footprint, and odd sizes round up. `HzbReference` is the CPU definition.
  - **Two-phase occlusion (item 51):**
    - `VisibilityPhase::Early` draws what was visible last frame.
    - The HZB is built from that depth.
    - `VisibilityPhase::Late` tests every in-frustum object against it, draws the ones not yet drawn that are not occluded, and rewrites a per-row, generation-tagged visibility history.
    - Newly visible and teleported objects are therefore found in the same frame.
    - `CameraCut` (`ResetLodHistory | ResetOcclusionHistory`) makes the early phase draw everything in view. Reused rows start without history.
    - `VisibilityStats` gained `Occluded`, `Deferred` and `AlreadyDrawn`.
  - **Validation:**
    - The official Linux configuration passes **485 cases / 11,248 checks** (was 476 / 7,445; most new checks are brute-force HZB and occlusion comparisons).
    - With EnTT supplied locally, **508 cases** pass. Both are clean under ASan/LSan/UBSan; the sanitizer found and fixed a use-after-free in the first `HzbBuilder` draft.
    - The new native smoke `GpuOcclusionTwoPhaseHzbRevealsNewlyVisibleObjects` runs the whole early → draw → HZB → late → draw pipeline on a real device and compares it with the CPU reference fed with the GPU's own HZB.
    - See [GPU visibility](GpuVisibility.md#two-phase-occlusion-item-51) and [the items 50–51 record](validation/Items50-51-2026-09-23.md).
  - **Still needed:** ~~desktop execution of both visibility smokes (28 native cases)~~ *(pass all profiles on the Windows desktop)*. Item **56** needs the engine runtime to draw from `GpuScene` + `GpuVisibility`; Phase 14 (items 58+) follows.
- **Earlier renderer checkpoint — items 49, 52, 53, 54, 55 and 57 implemented (2026-09-23):** GPU Scene rows are now culled, LOD-selected, binned and turned into indirect draws entirely on the GPU.
  - **RHI:** `CommandList::DrawIndexedIndirect`/`DrawIndexedIndirectCount` with `DrawIndexedIndirectCommand` (20 bytes). The Vulkan backend validates usage, alignment, stride, ranges and `maxDrawIndirectCount`, and the device now enables `multiDrawIndirect` and `drawIndirectFirstInstance`.
  - **`Renderer/Visibility`:**
    - `GpuViewRecord`/`BuildGpuViewRecord` extract depth-[0,1] frustum planes (convention-agnostic, so reverse-Z works unchanged).
    - `GpuVisibility` records one clear pass, a compute pass (`GpuVisibility.slang`) and an optional asynchronous statistics readback per view. The compute pass does drawability + sphere frustum culling (item 49), projected-error LOD selection with per-row hysteresis history reset by generation or `ResetLodHistory` (item 52), atomic compaction into bounded (material bin × index page) bins (items 53/54), and one `DrawIndexedIndirectCommand` + `GpuDrawRecord` per submesh with per-bin counts for `DrawIndexedIndirectCount` (item 55).
    - Persistent state (material-bin table, LOD history) uploads only when it changes.
    - `VisibilityStats` (item 57) accounts for every row and counts per-LOD usage and dropped draws.
  - **CPU definition:** `RunVisibilityReference` over the same rules (`VisibilityMath.h`); a 100k-row benchmark keeps the statistics consistent.
  - **Validation:**
    - The official Linux configuration passes **476 cases / 7,445 checks** (was 465 / 7,236).
    - With EnTT supplied locally, **499 cases** pass. Both are clean under ASan/LSan/UBSan.
    - The new native smoke `GpuVisibilityCullsBinsAndDrawsIndirect` draws a 64×64 grid through every bin with `DrawIndexedIndirectCount` over three frames (camera moves, edits, a camera cut) and compares commands, records, counts, statistics and pixels with the CPU reference.
    - See [GPU visibility](GpuVisibility.md) and [the items 49–57 record](validation/Items49-57-2026-09-23.md).
  - **Still needed:** ~~desktop execution of the new smoke (27 native cases)~~ *(passes all profiles on the Windows desktop)*. Items **50/51** (HZB, occlusion) wait for the reverse-Z/depth-convention decision, and item **56** needs the engine runtime to draw from `GpuScene` + `GpuVisibility`.
- **Earlier renderer checkpoint — items 46, 47 and 48 implemented (2026-09-22):** the renderer now has a persistent render-facing object database that is not EnTT.
  - **`Render::GpuScene` (item 46):**
    - Stable `RenderObjectHandle` rows live in two device-local std430 buffers: 64-byte `GpuInstanceRecord` (local bounds, mesh index + generation, material set, object id, flags, skin, LOD bias, generation) and 96-byte `GpuTransformRecord` (current and previous world 3x4).
    - `GpuRecordBuffer<T>` provides CPU mirrors with dirty-row tracking. Each import uploads only changed rows, batched into contiguous runs, as one staged graph pass per buffer.
    - Transform-only changes never touch instance rows. Previous transforms follow the frame before, and "settle" once when motion stops.
    - Destroyed rows go dead at the next upload and are reused only after their timeline point.
    - `GpuSceneRecords.slang` mirrors the records, and the shader compiler now reflects structured-buffer element layouts, so a test proves the C++ and Slang layouts agree.
  - **`Engine::RenderExtractor` (item 47):** maps `(scene, entity, part)` of the new `MeshRenderer` component to render objects.
    - It applies only EnTT construct/update/destroy signals, the scene `TransformSystem`'s per-frame dirty list, and pending mesh residency (`AssetResidencyService::ResolveRenderMesh`, which keeps staged mesh bounds).
    - Scene unload retires everything through `ReleaseAll(lastUse)`.
  - **Item 48:** 100k-object stress runs through the GPU Scene and through extraction. Frames that change 1% upload about 1% of the bytes, and a static frame uploads nothing. The opt-in native smoke `GpuSceneHundredThousandObjectsDirtyUploads` probes all 100k rows on the GPU with a compute shader every frame.
  - **Validation:**
    - The official Linux configuration passes **465 cases / 7,236 checks** (was 455 / 7,086).
    - With EnTT supplied locally, **488 cases** pass, including the Scene/ECS suites and the five extraction cases. Both are clean under ASan/LSan/UBSan.
    - See [GPU Scene](GpuScene.md) and [the items 46–48 record](validation/Items46-48-2026-09-22.md).
  - **Still needed:** desktop execution of the new smoke (26 native cases), and a Windows build of the Scene/ECS extraction suite in the full configuration. *(The developer then passed the default suite and all 26 native smokes under every profile.)* Phase 13 followed.
- **Previous renderer checkpoint — item 45 implemented (2026-09-22):** one persistent descriptor table now holds every sampled texture and sampler a shader may index. The RHI gained the bindless contract it previously rejected: Slang's unbounded `Texture2D T[]`/`SamplerState S[]` arrays reflect as runtime-sized (Count 0) bindings; `PipelineLayoutDesc::DescriptorSpaces` supplies explicit, shareable spaces that size them and may be visible to more stages than the program; `DescriptorBindingDesc::UpdateAfterBind` (with `PartiallyBound`) maps to Vulkan partially-bound, update-after-bind and update-unused-while-pending bindings in update-after-bind pools, checked against the update-after-bind limits; such elements stay writable after binding and while other elements are in flight; and a table now binds to any pipeline whose layout defines its space identically. The last feature, `descriptorBindingUpdateUnusedWhilePending`, is enabled optionally and reported as `GraphicsCapabilities::BindlessDescriptors`. On top of that, `Render::BindlessResourceTable` keeps separate texture and sampler index spaces on `GpuResourceRegistry`, keeps permanent fallbacks at element 0, writes new elements immediately, and rewrites released elements to the fallback only after their timeline point before FIFO reuse. `GpuSamplerCache` deduplicates samplers by description and counts references. `AssetResidencyService` registers a texture explicitly when it becomes Resident (`GetBindlessIndex`) and retires the element with the texture. The Linux configuration passes **455 cases / 7,086 checks** (was 443 / 6,867) and is clean under ASan/LSan/UBSan; see [GPU residency](GpuResidency.md#bindless-textures-and-samplers-item-45) and [the item 45 record](validation/Item45-2026-09-22.md). The new native smoke `BindlessTableTimelineSafeReuse` needs desktop execution (25 native cases). Items **46–48** (GPU Scene, extraction, stress) followed.
- **Previous renderer checkpoint — item 44 implemented (2026-09-22):** compiled `MeshAsset`/`TextureAsset` identities now reach GPU residency through explicit asynchronous state: `AssetResidencyService` runs `Queued → Reading → Decoding → WaitingForGpuUpload → Uploading → Resident` (or `Failed` with an `AssetError`) with bounded `AsyncIoService` reads, `DecodeSasset` hash validation/decoding on `JobSystem` workers, owner-thread `PublishSasset`, request-ordered staging under a per-update byte budget, graph-recorded uploads committed against the executor's completion value, and CPU assets released once their GPU copy is staged. `MeshGeometryPayload` interleaves vertex streams, derives layout ids and maps primitives/LODs/meshlets; `GeometryHeap` gained a `GpuSubmeshRecord` row buffer (draw ranges with material slots) and LODs became submesh ranges; `TextureResidency` uploads uncompressed native mip chains through the graph with timeline-retired RHI objects (block-compressed/KTX2 variants are rejected explicitly for now). A real pre-existing `AsyncIoService` leak was fixed along the way: each read job captured its own request state while the request held the job handle, so every request and its file bytes leaked; jobs now hold a weak reference and dispatched completion callbacks are dropped. The Linux configuration, now built **with** the asset compiler, passes **443 cases / 6,867 checks**, and the whole suite is clean under AddressSanitizer/LeakSanitizer/UBSan; see [GPU residency](GpuResidency.md) and [the item 44 record](validation/Item44-2026-09-22.md). The new native smoke `TextureResidencyMipChainUploadAndRetirement` needs desktop execution. Item **45** (bindless texture/sampler table) followed.
- **Windows desktop validation of items 41–43 (2026-09-22):** the developer's soft MSVC Debug build passed the default suite (461 cases / 10,592 checks) and all 23 native smokes under `core`, `sync`, `gpu` and `all` on the RTX 4070 Laptop GPU; see [the items 41–43 record](validation/Items41-43-2026-09-22.md).
- **Previous renderer checkpoint — items 41, 42 and 43 implemented (2026-09-22):** the RenderGraph now schedules staging itself: `CreateUpload`/`CreateReadback` declare buffers suballocated from executor-owned upload/readback arenas that are reset only after the predecessor wait, flushed before submission and bound to the submission's own completion value; `RenderGraphTransfers.h` adds whole/partial buffer and texture upload/readback passes, and `ReadWrite` + `CopyDestination` now means a preserving partial copy. `Renderer/Resources` adds `GpuHandle<Tag>` aliases and `GpuResourceRegistry` with timeline-retired records and FIFO slot reuse. `Renderer/Geometry` adds the paged `GeometryHeap`: best-fit ranges in large device-local vertex/index/meshlet pages (dedicated pages for oversized streams), stable 192-byte `GpuMeshMetadata` rows, per-page batched graph uploads, explicit `PendingUpload → Recorded → Uploading → Resident` residency, deferred frees and fragmentation metrics. The Linux foundation/RHI/shader configuration passes **402 cases / 6,384 checks** (was 380 / 5,977), including the 22 new CPU cases; the new graph/residency sources are also clean under ASan/UBSan and clang `-Wall -Wextra -Wpedantic -Wshadow`. The two new native smokes (`RenderGraphStagedTransfersAndReadback`, `GeometryHeapPagedUploadAndRetirement`) compile but were **not executed on a GPU** in this checkpoint's container (no video device), and Windows/MSVC was not rebuilt; both remain desktop gates. See [RenderGraph staging](RenderGraph.md#staged-transfers-item-41), [GPU residency](GpuResidency.md) and [the validation record](validation/Items41-43-2026-09-22.md). Dedicated transfer-queue ownership remains an open Phase 10 follow-up. Item **44** (asynchronous asset residency) is next.
- **Previous renderer checkpoint — item 40 complete:** RenderGraph now owns pass dependencies, initialization validation, culling, mip/layer states, barriers, transient pooling, imported/exported resources, GPU pass labels/timestamps and deterministic single-queue execution. Windows passes all 21 native smokes under all four profiles; Linux passes both new graph smokes under all four profiles on llvmpipe/Xvfb/Openbox. Default suites pass 439 Windows / 380 Linux cases. See [the graph contract](RenderGraph.md) and [item 40 evidence](validation/Item40-2026-09-12.md). Dedicated queue ownership transfers and async scheduling remain explicit Phase 10 follow-ups; item **41** is the next critical-path checkpoint.
- **Previous RHI checkpoint — item 39 complete:** all nineteen native cases pass core, synchronization, GPU-assisted and combined validation on Windows (RTX 4070 Laptop GPU) and Linux (Mesa llvmpipe on Xvfb/Openbox). Windows strict HDR also passes. The only accepted warning is the exact user-approved GPU-AV descriptor-limit startup advisory, retained in the reports; every other warning/error/drop still fails. See [the September 12 evidence and reproduction record](validation/Item39-2026-09-12.md). Linux physical-GPU coverage remains additional work; item **40** (RenderGraph) is implemented by the following checkpoint.
- **Previous RHI checkpoint — nested resource layouts/parameter blocks:** tool-side reflection now resolves explicit global/entry parameter blocks, resource-bearing constant buffers and nested structs into the existing RHI descriptor schemas. Relative bindings and child descriptor sets are accumulated independently, fixed resource arrays remain single bindings, and generated uniform buffers retain source paths and reflected byte ranges. The original Basic Slang sample now converts to an RHI interface. No additional desktop smoke was introduced by that checkpoint; the nineteen existing cases now pass the item **39** desktop matrix recorded above. See the September 12 Phase 9 checkpoint below.

- **File organization pass — Vulkan RHI and physics backends:** the monolithic `VulkanRhiBackend.cpp`, `JoltWorldBackend.cpp`, and `PhysXWorldBackend.cpp` — each previously a single file defining most or all of that backend's concrete types — have been split one-type-per-file under `Internal/`, `Resources/`, `Sync/`, `Commands/`, `Filters/`, and `Callbacks/` subfolders (see §0.2 for the rule and §33 for the target layout). This was pure code motion: no behavior changed. `VulkanSwapchain` was further split into a declaration-only header plus a `.cpp` for its non-trivial method bodies, matching the pattern already used by `VulkanQueue`. While validating the physics split, one genuine pre-existing latent bug was found and fixed: `JoltWorldBackend.cpp` defined `ToGlm(JPH::RVec3Arg)` unconditionally, but the header only declares it under `#ifdef JPH_DOUBLE_PRECISION`; since this project builds Jolt with `DOUBLE_PRECISION OFF` (where `RVec3Arg` aliases `Vec3Arg`), the unguarded definition collided with the `Vec3Arg` overload. The `.cpp` now matches the header's guard. No genuinely deprecated/dead code was found in either area to relocate into `Deprecated/`.
- **Engine module collapse — Visual Studio project topology:** the "Engine Modules" solution folder and every target that lived in it (`SwimCore`, `SwimMemory`, `SwimJobs`, `SwimPlatform`, `SwimInput`, `SwimCommands`, `SwimIO`, `SwimAssets`, `SwimPhysics`, `SwimPhysicsJolt`, `SwimPhysicsPhysX`, `SwimRhi`, `SwimRhiVulkan`) are retired as separate CMake targets. Their sources now compile directly into `SwimEngine` (or `SwimTests`/an example, whichever binary needs the same code — never both, to avoid duplicate-symbol link errors) and appear there as `source_group(TREE ...)` filters mirroring `Source/...` on disk, instead of as their own `.vcxproj` projects. This is a build-graph/solution-presentation change only: source-file organization within a module (§0.2) and the module dependency direction (§4.3's target graph, now a code-ownership graph) are unchanged. `Tests`, `Tools`, `Third Party`, and `Examples` solution folders are unaffected. Full rationale, the duplicate-symbol-avoidance rules, and the per-target consumer list: §4.3's "Engine module collapse checkpoint" and `docs/VisualStudioProjectStructure.md` §11.

- **Phase 1 — Platform foundation:** the SDL3-backed `Swim::Platform`/`Swim::Input` foundation, normalized window/input types, filesystem/mapped-file/dynamic-library APIs, native-window escape hatch, headless path, and generic-PCH cleanup are implemented. The old Win32 external-editor/`WM_COPYDATA` bridge is archived under top-level `Deprecated/` and has no active includes or build participation; future editor work is in-process engine UI. The remaining Phase 1 gates are runtime smoke coverage for the same `HelloWindow` API on both Windows and Linux and explicit Windows public-header validation. `SwimEngine`, Scene, Behavior, camera controls, gizmos, UI, and game behaviors now consume `Swim::Input::InputSystem` directly. The old `InputManager` wrapper is retired; input advances once after event pumping and before fixed/update consumers.
- **Retirement checkpoint — input, commands, and external editor:** `Source/Engine/Systems/IO` is gone. `Source/Engine/Input` owns normalized input; `Source/Engine/IO` owns async file IO; `Source/Engine/Commands` owns the new `Swim::Commands::CommandRegistry`. The old `InputManager`, `CommandSystem`, unused `SystemManager`, editor IPC, scene-JSON experiment, and disabled SceneSystem editor-command blocks are archived outside `Source/`. Active durable entity IDs/maps moved to `Scene/Identity`. Section **0.3** is the authoritative retirement inventory and gives the gates for moving the still-active legacy renderers/pools later.
- **Phase 2 — Engine ownership/configuration:** complete for the existing runtime. `SwimEngine::GetInstance()` is gone from first-party runtime dependency discovery, core systems have explicit typed ownership/lifecycle order, and graphics/physics backend choice is runtime configuration rather than a compile-time renderer selector.
- **Phase 3 — Jobs/IO/memory:** complete. `Swim::Jobs`, `Swim::IO`, `Swim::Memory`, mimalloc-backed frame/scratch allocation, and deterministic async/job shutdown are established before renderer/streaming expansion.
- **Phase 4 — Assets:** the engine-owned `Swim::Assets` identity/runtime schema, fastgltf importer, meshoptimizer path, KTX2/Basis metadata/transcode path, WebP/PNG/JPEG source-image compiler, compiler-side Draco decode, `.sasset` v1 writer/reader, development incremental cooker, and cooked-model compatibility residency path are implemented. Source codecs are owned by `SwimAssetCompiler`; they are not supposed to become shipping runtime model-import dependencies.
- **Phase 4 validation:** the Draco 1.5.7 embedded-consumer include-root issue remains isolated behind `Swim::AssetCompilerDraco`, and the supported Windows clean/soft builds compile/run the importer, source-image, development cook/load, and cooker validation targets automatically. The developer has confirmed the dependency-enabled Windows build path is already green, and the repository now contains the real `Assets` authoring tree supplied for this checkpoint (including the Sponza KTX/Draco variants, WebP sofa, barrel/test models, fonts, and textures). The build scripts continue to enforce that gate rather than relying on this one confirmation.
- **Phase 5 / Phase 6 handoff:** the scene foundation now includes explicit `SceneCatalog`/`SceneId`, headless/core/presentation separation, scene-owned Transform and mutation state, per-view Frustum state, an instance-owned behavior registry, durable `SerializedEntityId` identity, `AssetId` references, and a canonical right-handed / 0..1-depth camera convention. The previously split `SceneSerializer`/`SceneStorage`/`SceneToolingBridge`/`SceneSyncTracker` experiment is now archived under **top-level `Deprecated/`**, with no active includes/build participation: Scene creates none of it, performs no automatic scene JSON save/delta work, and sends no external-editor IPC. Critical-path items **22 through 38 are implemented**: PhysX and Jolt sit behind the same generic physics seam and parity contract; Slang is now the only first-party shader source language and generates both Vulkan SPIR-V and the isolated legacy OpenGL GLSL compatibility artifacts; the backend-neutral `Swim::Rhi` type/object contract defines formats, resource states, descriptor/capability vocabulary, adapters/devices/queues/swapchains, command objects, GPU resources, shader/pipeline objects, synchronization, and query pools without Vulkan types; an explicit runtime `GraphicsFactory` owns RHI backend registration/creation; and `Swim::RhiVulkan` now owns the Vulkan 1.3 instance/adapter/device/queue bootstrap through namespaced volk + vk-bootstrap plus SDL3-owned Vulkan WSI/swapchain presentation. `Swim::RhiVulkan` now also owns normal buffer/image allocation through VMA v3.4.0, with backend-neutral memory preferences mapped to VMA policy and VMA allocation names retained for diagnostics. Item 38 adds the reusable backend-neutral `FrameContextRing`, per-frame command-pool/list ownership, monotonic queue timeline submission, timeline-waited frame reuse, and deferred `RhiObject` retirement; the Vulkan backend now implements timeline semaphores, synchronization2 queue submission, and command-pool/list lifecycle. Swapchain replacement no longer performs a device-wide idle: it waits the supplied frame timeline and then uses a presentation-queue-only WSI completion fallback because core Vulkan presentation completion is not represented by the render timeline. **Item 39 is complete for the documented Windows hardware and Linux software-Vulkan desktop matrix.** Its clear/transfer implementation is now in place (see the 2026-09-05 Phase 9 checkpoint): synchronization2 barriers, dynamic-rendering clears, CPU buffer access, buffer/image copies, and an opt-in real-driver clear/readback/presentation smoke. Slang-backed procedural/indexed triangle drawing is implemented in the following Phase 9 checkpoint. Reflected descriptor tables, samplers, and sampled 2D texture drawing are now implemented in the following Phase 9 texture checkpoint. Windows hardware and Linux software-Vulkan validation, including resize/minimize/restore, now pass; see the September 12 validation record. Do not spend that runway over-polishing the current BVH/scene/GPU-dirty machinery that the later renderer/GPU Scene phases are expected to replace.
- **Testing:** the whole runnable test corpus is one program, `SwimTests`, built from self-registering suites under `Source/Tests/Suites/<group>/`. Adding coverage is a new `.cpp` in the right dependency group, never a new CMake target. The Windows clean/soft builds and the Linux builds run the complete suite, so coverage is continuously exercised instead of depending on a hand-maintained list of phase gate targets. Per-module public-header compile gates stay separate because their value is their narrow link surface. See section 32.
- **Shipping asset policy:** development auto-cook is intentionally convenient and currently enabled by default when `SwimAssetCompiler` exists. Shipping/release packaging is intended to disable `SWIM_ENABLE_DEV_ASSET_AUTOCOOK`, pre-cook with `SwimAssetCooker`, and run from compiled `.sasset`/future `.spack` data without glTF/Draco/WebP source-import code. Final packaging presets and `.spack`/memory-mapped streaming are later work, so do not confuse the current development executable with the final shipping dependency closure.

Companion documentation for the current generated solution and asset pipeline:

- `docs/VisualStudioProjectStructure.md` — what the projects/folders in the generated Visual Studio solution mean, how they depend on one another, and what a normal build actually compiles.
- `docs/SassetCookPipeline.md` — the source -> import -> cook -> `.sasset` -> runtime path, including development auto-cook versus release/shipping usage.
- `docs/RenderGraph.md` — graph declaration/compilation/execution contracts, including executor-staged transfers.
- `docs/GpuResidency.md` — generational GPU registries, the paged GeometryHeap, TextureResidency and the asynchronous asset residency service.
- `docs/GpuVisibility.md` — GPU-driven culling, LOD, binning and indirect draw generation over the GPU Scene.
- `docs/Materials.md` — material templates, instances and reflected parameter layouts.

---

## 0. The implementation rule that controls this entire plan

The engine must be built from the bottom of the dependency graph upward.

A subsystem may depend on a lower layer. A lower layer must never know about a higher layer.

```text
Game / Application / Tools
            |
            v
Scene / Gameplay / UI / Animation
            |
            +----------------------+----------------------+
            |                      |                      |
            v                      v                      v
         Render                 Physics                 Audio
            |                      |                      |
            v                      v                      |
           RHI              Physics Backend              |
            |                      |                      |
            +-----------+----------+----------------------+
                        |
                        v
              Assets / Jobs / Async IO
                        |
                        v
             Platform / Window / Input
                        |
                        v
              Windows / Linux / Apple / Android
```

This ordering is not cosmetic. It prevents the engine from building a modern renderer, asset system, or physics integration on top of temporary Windows-only handles, global singletons, synchronous file loading, or backend-specific CPU data and then having to perform surgery later.

### 0.1 Non-negotiable dependency rules

- [x] Platform abstractions exist before the modern RHI consumes a window or surface.
- [x] Input is platform-neutral before gameplay APIs are rewritten around it.
- [x] File/path/IO abstractions exist before the asset pipeline is made authoritative.
- [x] The jobs system exists before renderer extraction, asset processing, animation, streaming, and other parallel systems are expanded.
- [x] Asset identity and ownership are fixed before the new renderer starts storing mesh/material/texture references. *(New work uses engine-owned `Swim::Assets::AssetSystem`, typed generational handles, and backend-neutral runtime asset schemas; the old renderer pools remain transitional migration targets only.)*
- [x] Scene ownership and transform dirty tracking are fixed before GPU Scene extraction is implemented.
- [x] Shader reflection contracts are defined before descriptor/pipeline layouts become entrenched in the RHI.
- [x] The RHI contract is defined before Vulkan implementation details spread through new renderer code.
- [x] Physics components contain backend-neutral handles before Jolt and PhysX coexist. *(PhysX and Jolt now coexist behind `BodyHandle`/`ShapeHandle`/`PhysicsMaterialHandle` and the shared `IPhysicsBackend`/`IPhysicsWorldBackend` contracts.)*
- [x] No new code reaches through `SwimEngine::GetInstance()` to discover dependencies.
- [x] No new public generic header includes Win32, Vulkan, OpenGL, PhysX, Jolt, SDL implementation details, or source-importer types.
- [ ] Source import is never the normal shipping runtime asset path.
- [ ] OpenGL compatibility never lowers the design of the modern RHI.

### 0.2 File organization rule: no monolithic implementation files

This is a code-hygiene rule, not a dependency rule, but it governs every phase below the same way: a large system is organized by putting each concrete thing in its own file, not by writing less code. Splitting a file never changes what it computes; it only changes where the compiler finds it.

- **One concrete type per file.** If a backend or subsystem defines several concrete classes/structs (a device, a swapchain, a command pool, a queue, a buffer, a filter, a callback), each one gets its own header (and a `.cpp` when its methods have real logic worth keeping out of the header), named after the type. A single `.cpp` or `.h` that defines a dozen unrelated classes is the failure mode this rule exists to prevent, regardless of how small each individual class is.
- **Trivial accessors stay inline; real logic moves to a `.cpp`.** A one-line getter/constructor can stay in the header next to the class declaration. A method with branches, error handling, or more than a few lines of work is declared in the header and defined out-of-line in a matching `.cpp`. `VulkanQueue`/`VulkanQueue.cpp` and `VulkanSwapchain`/`VulkanSwapchain.cpp` are the reference examples.
- **Shared helpers get their own header, not a copy in every file that needs them.** Small stateless functions, format/type conversion tables, and small shared structs used by several of a backend's files belong in an `Internal/` header (plus a `.cpp` for the definitions) scoped under that backend, not duplicated per file and not left anonymous-namespace-private to whichever file happened to need them first. When two backends need near-identical helpers (for example Jolt and PhysX both validating a pose or testing collision-layer masks), each backend keeps its own copy in its own namespace — never promote both into one shared namespace, or their identical-looking function signatures collide at link time.
- **Group by role with plain subfolders**, named for what the files inside them are, not for the backend that happens to own them: `Internal/` for shared, backend-private helpers and bootstrap state; `Resources/` for owned GPU/engine resources (buffers, textures); `Sync/` for synchronization primitives (semaphores, fences, timelines); `Commands/` for command recording objects; `Filters/` for query/collision filter callbacks; `Callbacks/` for engine-to-third-party callback objects. Section 33 shows this applied to the current Vulkan RHI and Jolt/PhysX physics backends; use the same shape for the next backend or large system rather than inventing new folder names per system.
- **Dead/superseded code moves to `Deprecated/`, it does not linger commented-out or dead-but-compiled in the active tree.** The top-level `Deprecated/` folder is outside `Source/`, so CMake's source globs exclude it automatically; that is what makes it safe to keep old material there for reference instead of deleting it outright.
- **A file split is reviewed like any other change:** every extracted file should compile on its own (not just as part of one giant translation unit), and the split should be checked line-for-line against the original so nothing is silently dropped or duplicated — see the 2026-09-05 "file organization checkpoint" entries under Phase 6 and Phase 9 for the technique this project actually used.

### 0.3 Replacement and retirement are part of completion

A replacement is not complete merely because a new API or folder exists. Its callers must migrate, ownership/frame/shutdown behavior must be wired, and the superseded implementation must leave the active tree. This rule applies to every remaining roadmap phase.

- [x] Use one top-level `Deprecated/` tree, outside `Source/`, with historical paths mirrored below it and a short replacement/status index in `Deprecated/README.md`.
- [x] Retired means **zero active includes, calls, target sources, link dependencies, or runtime packaging**. Do not put still-required code in `Deprecated/` and then add include paths or source globs back to it.
- [x] Remove old compatibility getters/wrappers when callers are migrated. `GetInputSystem` / `SetInputSystem` replace the old manager accessors; there is no alias back to the retired class.
- [x] Move dormant editor blocks out of live class declarations/definitions. The archive can preserve historical fragments; active files must not carry large `#if 0` implementations.
- [x] Keep live foundations distinct from retired experiments. `Scene/Identity` owns durable IDs/maps even though their first consumers included scene serialization.
- [x] Deliver the complete clean repository layout. Move retired files directly outside `Source/`; do not add migration ledgers, deletion scripts, or CMake tombstones for old paths.
- [ ] As later replacements become authoritative, extend this inventory/ledger, migrate consumers, remove obsolete dependencies, update solution/docs, and verify no active references remain before checking off retirement.

#### Current subsystem inventory

| Concern | Authoritative active implementation | Retirement status / next gate |
| --- | --- | --- |
| Platform windows/input events | `Engine/Platform` (SDL3 behind Swim types) | Old editor IPC archived. Generic native/external-window capabilities remain active Platform features. |
| Input state/actions | `Engine/Input/InputSystem` | Directly injected into all current engine/gameplay consumers; old `Systems/IO/InputManager` archived. `Float2` stays platform-neutral; GLM conversion occurs at camera/UI/gizmo consumers. |
| File IO | `Engine/IO/AsyncIoService`, `Engine/Platform/FileSystem` | One async IO directory. Platform filesystem primitives and scheduled/range IO are distinct layers, not competing managers. |
| In-process commands | `Engine/Commands/CommandRegistry`, target `Swim::Commands` | Old `Systems/IO/CommandSystem` archived. Keeps current play/pause/resume/stop/edit/game commands; no IPC, fake per-frame lifecycle, or unused typed-command generator. |
| Engine ownership | Explicit typed services in `SwimEngine` | Unused string/type-map `SystemManager` archived. `Machine` remains in use by scenes/behaviors/current runtime systems and is not falsely marked retired. |
| Scene identity | `Systems/Scene/Identity` | IDs/maps remain runtime foundations. Old JSON serializer/storage/sync and external editor command fragments are archived. Future persistence is an explicit optional service, not automatic per-frame JSON work. |
| Jobs/memory | `Engine/Jobs`, `Engine/Memory` | Modern shared services are active. `ParallelUtils` still has renderer callers; retain only as a documented adapter until those callers migrate. |
| Asset identity/import/cook | `Engine/Assets`, `Tools/AssetCompiler` | Modern asset authority is active. `MeshPool`, `TexturePool`, `MaterialPool`, `FontPool`, `LegacyRenderBinding`, and renderer-facing `Texture2D`/mesh data still serve the current renderer; retire each after its matching replacement and consumers migrate: geometry/texture residency (items 41–47), materials (58–59), and text/font services (79). |
| GPU residency | `Systems/Renderer/Resources`, `Systems/Renderer/Geometry`, `Systems/Renderer/GpuScene`, `Systems/Renderer/Visibility`, `Systems/Renderer/Residency` | New backend-neutral layers (items 42–46, including `BindlessResourceTable`/`GpuSamplerCache` and the persistent `GpuScene`, plus GPU-driven visibility, items 49/52–55/57); no legacy consumer yet. `SceneBVH` culling and the CPU visible list in `VulkanIndexDraw` stay until the engine draws through `GpuVisibility` (item 56). `Systems/Scene/RenderExtraction` + `Components/MeshRenderer.h` (item 47) are the EnTT producer; the legacy `Material`/`LegacyRenderBinding` component and `SceneBVH` renderable slots stay until the modern renderer draws the GPU Scene. The legacy renderer's own bindless path (`VulkanDescriptorManager`) stays until the modern renderer binds `BindlessResourceTable`. `MeshPool`/`TexturePool`/`VulkanIndexDraw` paths stay active until the modern renderer draws from `GeometryHeap`/`TextureResidency` and the engine constructs `AssetResidencyService`; only then can those pools be archived. |
| Modern graphics backend | `Systems/Renderer/RHI/Backends/Vulkan` | RHI clear/transfer, triangle/textured pipelines and explicit vertex/instance input are implemented. Item 39 desktop validation passes on Windows hardware and Linux software Vulkan (see the validation record). It is not yet the game renderer. |
| Current game rendering | `Systems/Renderer/Vulkan`, `Systems/Renderer/OpenGL`, current `Renderer` facade | **Active legacy**, not retired. First reach the RHI/render-graph/residency/GPU-scene replacement gates, move game presentation onto them, then archive the replaced Vulkan path and facade pieces. OpenGL may remain an explicitly built compatibility renderer under `Legacy/OpenGL` only while it is intentionally supported; archive it when support and consumers are removed. `Legacy/` therefore means active compatibility; `Deprecated/` means never used. |
| Physics | Generic physics API plus selectable PhysX/Jolt backends | Both are current implementations of the same contract, not an old/new duplicate pair. Preserve both; retire only obsolete bypasses or backend-leaking adapters. |

#### Input/command/editor retirement checkpoint — 2026-09-05

- [x] Replace the engine-owned `InputManager` with `Swim::Input::InputSystem`; migrate Scene, Behavior, demo input, mouse callbacks, camera controls, gizmos, and UI hit/drag code.
- [x] Publish input once after the event pump and frame-skip decision, before fixed simulation. Both fixed and presentation/update code read that same snapshot. Input edges are frame-scoped; this does not implement a separate per-tick input history/replay system.
- [x] Replace the active command dispatcher with a transport-free `Swim::Commands::CommandRegistry`, built/tested independently of the Windows runtime. Commands accept raw argument strings; optional outer parentheses, quoted strings, empty quoted arguments, and escaped quotes/backslashes are supported. Malformed input does not invoke callbacks; callback replacement/self-removal is safe.
- [x] Archive the obsolete input/command manager classes, unused `SystemManager`, Platform editor IPC, disconnected scene-JSON experiment, and dormant SceneSystem editor-command blocks. Runtime entity identity moves to `Scene/Identity`.
- [x] Update the architecture verifier to reject references to retired classes and enforce the input publication order, command module ownership, new identity location, and absence of active references to archived code.
- [x] Deliver a full repository ZIP for extraction into a fresh directory; the old active paths are already absent.
- [x] Resume item 39 with the Slang-backed triangle pipeline checkpoint below. The following texture checkpoint implements sampled 2D drawing; real Windows/Linux GPU validation remains open, and the current game still uses the transitional renderer.

**Validation:** the dependency-enabled GCC 13/C++20 Debug foundation build passes **91 cases / 597 checks**, including four command-registry tests and two direct-input frame tests added here. Platform/Input and RHI public-header gates and the architecture verifier pass. All eleven modified Scene/SceneSystem/Behavior/camera/gizmo/UI/gameplay `.cpp` consumers syntax-check with the real pinned EnTT/GLM/Vulkan headers and generated GL declarations; the scratch-only PCH copy normalizes its existing Windows include separators for GCC. The full `SwimEngine.cpp`/Windows executable remains an MSVC gate because the still-active OpenGL backend requires WGL/Windows headers. Asset/shader compilers and concrete physics backends were not rebuilt in this foundation validation; GPU/window validation remains pending.


---

# Part I — Architecture established before renderer work

## 1. Target engine composition

The long-term runtime should look approximately like this:

```text
Swim::Engine
  |
  |-- Core
  |     |-- IDs / handles
  |     |-- Result / diagnostics
  |     |-- memory arenas / utilities
  |     `-- configuration
  |
  |-- Platform
  |     |-- Application host
  |     |-- Window system
  |     |-- normalized events
  |     |-- filesystem roots / paths
  |     |-- mapped files
  |     |-- dynamic libraries
  |     |-- time / thread helpers
  |     `-- native escape hatches
  |
  |-- Input
  |     |-- keyboard / mouse
  |     |-- controller
  |     |-- text / IME
  |     `-- action maps
  |
  |-- Jobs
  |     `-- enkiTS-backed task scheduler
  |
  |-- IO
  |     `-- async reads / range reads / streaming
  |
  |-- Assets
  |     |-- AssetId / typed handles
  |     |-- registry / dependency graph
  |     |-- compiled runtime formats
  |     `-- residency / streaming
  |
  |-- Scene
  |     |-- EnTT registry
  |     |-- transform hierarchy
  |     |-- behavior runtime
  |     |-- CPU spatial queries
  |     `-- render extraction producer
  |
  |-- Physics
  |     |-- generic world/body/query API
  |     |-- PhysX backend
  |     `-- Jolt backend
  |
  |-- Render
  |     |-- RHI
  |     |-- Vulkan backend
  |     |-- render graph
  |     |-- GPU Scene
  |     |-- GPU visibility
  |     |-- geometry residency
  |     |-- Slang shader/material system
  |     |-- PBR / IBL
  |     |-- Clustered Forward+
  |     |-- shadows
  |     |-- particles
  |     `-- post processing
  |
  |-- Animation
  |-- Audio
  |-- Runtime UI
  `-- Debug / tooling bridges
```

The key point is that `Swim::Engine` composes these systems. It is not itself the global service locator through which every subsystem talks to every other subsystem.

---

## 2. Repository architecture audit / modernization baseline

The repository already contains useful renderer, scene, BVH, text, behavior, physics, and indirect-drawing work. The modernization should preserve good algorithms and working behavior while correcting the dependency and ownership model around them.

> **Status note:** this audit records the pre-refactor/problem baseline that motivated the plan. The phase checklists and dated checkpoints below are the authoritative current implementation state.

### 2.1 Platform leakage is currently foundational

Current examples that must be eliminated from generic layers:

- `PCH.h` globally includes `Windows.h`, Vulkan Win32 headers, WGL, Vulkan, and OpenGL.
- `SwimEngine` owns `HWND`, `HINSTANCE`, a Win32 window procedure, Win32 focus logic, window messages, and editor `WM_COPYDATA` messaging.
- `Renderer::Create()` takes an `HWND`.
- `InputManager` consumes `UINT/WPARAM`, Win32 virtual key codes, `GetCursorPos`, `GetFocus`, and an `HWND`.
- `CameraSystem` asks the global engine for window dimensions.
- the build intentionally cannot run the full engine on Linux because the runtime code is still Win32-bound.

**Target:** only the Platform implementation and optional Windows-specific compatibility bridges know what an `HWND` is.

### 2.2 Renderer choice currently leaks into unrelated CPU code

Backend selection currently uses compile-time `SwimEngine::CONTEXT` checks in engine creation, scene wiring, textures, camera projection, cubemap handling, transforms, resize logic, and other places.

Examples of the architectural issue:

- `Camera::GetProjectionMatrix()` changes its projection based on the selected graphics API.
- `Transform` contains Vulkan/OpenGL-specific screen-depth behavior.
- `Texture2D` chooses its upload implementation with `if constexpr`.
- `SceneSystem` wires concrete Vulkan or OpenGL renderer pointers into each scene.

**Target:** backend selection happens once during startup. Generic camera, transform, scene, asset, and gameplay code never branches on the active graphics API.

### 2.3 Generic render data is backend-specific today

Several files under renderer `Core` are not actually backend-neutral:

- `Vertex.h` contains `VkVertexInput*` descriptions and direct OpenGL vertex attribute calls.
- `MeshBufferData.h` includes Vulkan and OpenGL buffer headers.
- `MeshBufferData::GenerateBuffersAndAABB()` immediately calls the global renderer to upload data.
- `Texture2D` directly stores `VkImage`, `VkDeviceMemory`, `VkImageView`, OpenGL texture IDs, and a bindless Vulkan index.
- `TextLayout.h` includes Vulkan GPU instance structures.

**Target:** CPU asset/runtime structures describe data. RHI/backend code decides how that data is represented on a GPU.

### 2.4 Asset ownership is too global and too eager

The current `MeshPool`, `TexturePool`, `MaterialPool`, and `FontPool` are process-wide singletons. They rely heavily on `shared_ptr`, names, global maps, and synchronous operations.

Important problems to correct:

- `MeshPool` registration performs GPU upload as a side effect.
- mesh deduplication can linearly byte-compare every existing mesh.
- `TexturePool::LoadAllRecursively()` eagerly walks a hard-coded asset directory.
- `Texture2D` loads from disk and uploads to the GPU in its constructor.
- `Texture2D` has a static raw-pointer cleanup set and global Vulkan texture index state.
- `MaterialPool` directly parses GLB, decodes images, creates textures, registers meshes, and registers materials.
- `MaterialData` owns a mesh pointer, mixing geometry identity with material identity.
- `CompositeMaterial` is effectively a vector of mesh/material bundles rather than a proper model/submesh/material-slot representation.
- `FontPool` recursively loads every font rather than participating in a unified asset lifetime model.

**Target:** source import, compiled asset data, CPU residency, GPU residency, material identity, mesh identity, and scene instances are separate concepts.

### 2.5 Global state is used as dependency injection

Current global/static access patterns include:

- the global `SwimEngine` instance;
- `MeshPool`, `TexturePool`, `MaterialPool`, and `FontPool`;
- `EntityFactory`;
- `BehaviorFactory`;
- a render-specific global thread pool;
- static global transform dirty lists/versioning;
- a static global camera frustum;
- static scene preregistration storage.

This makes multiple engines, multiple worlds, isolated tests, headless tools, and parallel scene work unnecessarily difficult.

**Target:** ownership is explicit. Registry-like systems are owned by an engine/application/tool context. Global statics are limited to true immutable constants or deliberately process-global infrastructure.

### 2.6 The current `Renderer` facade mixes abstraction levels

The existing `Renderer` interface is not a useful final RHI boundary. It currently combines several unrelated responsibilities:

- `Create(HWND, width, height)` makes presentation/platform creation part of the generic renderer API;
- `GetCubeMapController()` exposes a high-level environment feature through the renderer base class;
- `UploadMeshToMegaBuffer()` makes asset residency a direct renderer operation;
- `VirtualCanvasWidth/Height` places UI layout policy in the renderer interface;
- `Vertex.h` is pulled into the interface even though its binding declarations are backend-specific.

Trying to extend this interface until Vulkan, D3D12, Metal, and legacy OpenGL all fit would produce the wrong abstraction.

**Target:** split responsibilities cleanly:

```text
Platform presentation/window
        |
        v
RHI device/swapchain/resources/commands
        |
        v
RenderSystem / RenderGraph / GPU Scene
        |
        +--> Environment
        +--> Materials
        +--> Debug
        `--> UI composition
```

UI virtual-canvas policy belongs to UI/view configuration. Environment/cubemap behavior belongs to the high-level renderer. Mesh upload is an asset-residency operation using RHI transfer facilities.

### 2.7 Scene registration, persistence, and editor transport are coupled

The current scene/tooling path combines concerns that need independent lifetimes:

- `REGISTER_SCENE`/`SceneRegistrar` creates scene instances through static initialization and stores them in a static preregistration vector;
- `SceneSystem` then injects engine/input/camera/concrete-renderer pointers after startup;
- serialized parent references use raw integral `entt::entity` values, which are runtime identities rather than durable scene identities;
- serialized material/model references are reconstructed from material names and source file paths;
- `SerializedSceneManager` performs filesystem writes itself and also sends the same representation to the editor through `WM_COPYDATA`;
- command registration and editor transport are wired directly into `SceneSystem`.

These choices make scene persistence, tools, runtime scene creation, and engine startup depend on one another unnecessarily.

**Target:** use explicit scene factories/catalog registration, stable persisted entity IDs, `AssetId` references, a pure scene serializer/deserializer, a separate storage service, and a separate optional editor/tool transport. Runtime `entt::entity` values remain implementation details of a loaded scene.

### 2.8 Scene and gameplay layers know too much about engine internals

Current `Scene` objects cache `SceneSystem`, `InputManager`, `CameraSystem`, concrete Vulkan/OpenGL renderer pointers, debug systems, editor serialization systems, and a physics world. Behaviors cache shared system pointers. `EntityFactory` targets the globally active scene.

Transform operations sometimes discover the active scene through the global engine, which is unsafe for multiple scenes and makes a component depend on application state.

**Target:** a scene owns scene-local state; engine services are passed through explicit context interfaces; scene components do not discover the active engine or renderer.

### 2.9 Transform and view state are process-global in places

The transform component contains static dirty state shared by every scene. Frustum state is static and assumes one camera/view.

That conflicts with:

- multiple scenes;
- multiple windows;
- mirrors/portals/render targets;
- editor/game views;
- shadow views;
- threaded render extraction.

**Target:** dirty tracking is per scene and view/frustum state is per render view.

### 2.10 Physics has a generic runtime boundary with PhysX/Jolt baseline parity

`Rigidbody.h`, `PhysicsSystem`, and `PhysicsWorld` expose only Swim-owned handles/descriptors/query/event contracts. PhysX lives behind `IPhysicsBackend` / `IPhysicsWorldBackend` in `Swim::PhysicsPhysX`; Jolt v5.6.0 lives behind the same contracts in `Swim::PhysicsJolt`; scene/EnTT synchronization remains separately owned by `ScenePhysicsBridge`. No PhysX/Jolt object pointer is stored in the generic Rigidbody component, and runtime backend choice does not require gameplay-side branching.

**Remaining physics target:** collision-mesh import/cooking and backend-specific compiled convex/triangle payloads belong to the asset/compiler path, followed later by fixed-step/interpolation policy. Do not introduce runtime source-mesh cooking merely to make the two backends look superficially feature-complete.

### 2.11 The current Vulkan implementation contains ideas worth keeping

The Vulkan indexed-draw work already explores:

- large shared geometry buffers;
- stable renderable slots;
- bindless textures;
- indirect drawing;
- persistent world packets;
- dirty transform updates;
- compute culling;
- GPU BVH snapshot data;
- indirect-count support;
- CPU BVH fallback/reference paths.

The problem is not the performance intent. The problem is that this functionality is concentrated in a very large Vulkan-specific class and remains coupled to EnTT entities, material `shared_ptr`s, global engine state, hand-managed Vulkan allocations, and legacy CPU packet paths.

**Target:** preserve the performance concepts while redistributing them into GPU Scene, GeometryHeap, Visibility, RenderGraph, RHI resources, and explicit scene extraction.

### 2.12 Vulkan resource lifetime and synchronization should be rebuilt on modern primitives

The current Vulkan code still performs many direct `vkAllocateMemory`/`vkFreeMemory` operations, ad-hoc staging allocations, fence-based frame synchronization, explicit pipeline barriers scattered across classes, queue waits, and multiple `vkDeviceWaitIdle` paths.

**Target:** VMA, persistent upload/readback arenas, timeline-based retirement, synchronization2, render-graph resource state tracking, and deferred destruction.

### 2.13 Text/UI needs a real text and UI layer

Current text code has useful MSDF work, but:

- UTF-8 decoding is handwritten and explicitly incomplete for 4-byte code points;
- shaping is based on direct glyph/kerning lookup rather than full Unicode shaping;
- UI layering is mixed into `Transform` and clip-space Z rules;
- UI interaction lives partly as scene behaviors/gizmo logic.

**Target:** FreeType + HarfBuzz + MSDF atlas generation/caching, plus a retained runtime UI system with its own layout and hit-testing model. World-space text can still be a scene/render feature.

### 2.14 Threading should become engine-wide infrastructure

The current `ParallelUtils` render thread pool is useful experimentation, but renderer-only global worker infrastructure is the wrong final ownership model.

**Target:** one general jobs service, preferably backed by enkiTS, is shared by render extraction, animation, asset work, streaming, scene tasks, and other CPU-parallel systems.

---

## 3. Hard architecture boundaries

- [x] `Swim::Platform` is the only generic runtime layer allowed to include OS-native window/process APIs. *(Native Win32 use is confined to Platform/internal or backend-specific implementation code; generic/public headers are guarded by verification.)*
- [x] `Swim::RhiVulkan` is the only normal layer allowed to include Vulkan implementation types. *(The modern backend lives under `RHI/Backends/Vulkan`; its public factory header is Vulkan-free and the architecture verifier rejects Vulkan/volk/vk-bootstrap leakage into generic RHI headers.)*
- [ ] `Swim::RhiD3D12` and `Swim::RhiMetal` can be added without changing high-level renderer contracts.
- [x] `Swim::PhysicsPhysX` is the only normal layer allowed to include PhysX implementation types. *(Generic Physics/Rigidbody/Scene code is verifier-guarded against PhysX/Jolt implementation types.)*
- [x] `Swim::PhysicsJolt` is the only normal layer allowed to include Jolt implementation types. *(Jolt includes/types are confined to `Systems/Physics/Backends/Jolt` and its private dependency target; the architecture verifier rejects Jolt leakage into generic physics.)*
- [x] SDL types do not become the public engine API. SDL is the Platform/Input implementation library.
- [x] fastgltf types do not escape the asset importer/tool boundary. *(fastgltf is private to `SwimAssetCompiler` and included only by `GltfImporter.cpp`; public importer/intermediate/runtime asset headers are Swim-owned.)*
- [x] enkiTS types do not become gameplay APIs. *(enkiTS is private to `Swim::Jobs`; gameplay/renderer-facing APIs use Swim job types.)*
- [x] Persisted scene references never use raw `entt::entity` values as durable identity. *(Scene persistence/editor transport use scene-owned `SerializedEntityId` values.)*
- [x] Scene serialization is independent from filesystem storage and editor/IPC transport. *(`SceneSerializer`, `SceneStorage`, `SceneToolingBridge`, and `SceneSyncTracker` are separate responsibilities.)*
- [x] Static initialization does not construct live Scene instances or require Engine services. *(Static scene registration stores constructor metadata; runtime Scene instances are created per engine.)*
- [ ] RHI contracts do not contain UI canvas policy or high-level environment features.
- [x] Material objects do not own meshes. *(Runtime `Material*Asset` types are geometry-free; transitional legacy `MaterialData` is geometry-free and draw-time pairing lives in `LegacyRenderBinding`.)*
- [x] Mesh assets do not own backend GPU buffers. *(Runtime `MeshAsset` is backend-neutral; the transitional legacy CPU `Mesh` no longer embeds `MeshBufferData`, which is owned separately by renderer residency.)*
- [x] Texture assets do not own raw Vulkan/OpenGL objects. *(Runtime `TextureAsset` stores CPU/runtime metadata and payload only; legacy `Texture2D` remains a compatibility renderer object pending pool removal.)*
- [ ] Constructors do not perform hidden disk IO or synchronous GPU uploads.
- [ ] Scene/ECS objects do not store raw RHI resources.
- [x] Scene/ECS objects do not store PhysX/Jolt pointers. *(Rigidbody stores a generational `BodyHandle`; `ScenePhysicsBridge` talks only to generic `PhysicsWorld`.)*
- [ ] RHI objects do not know about EnTT.
- [ ] RenderGraph does not know about EnTT.
- [ ] GPU Scene does not require EnTT.
- [x] Backend selection is runtime configuration, not `constexpr` source branching. *(Graphics/physics selection is parsed into `EngineConfig` before backend resources are created; unsupported compiled-out choices fail explicitly.)*
- [ ] OpenGL is isolated as a legacy compatibility renderer and does not dictate RHI concepts.
- [x] Editor integration is optional infrastructure layered around the runtime, not a prerequisite for engine initialization. *(Window hosting/focus/`WM_COPYDATA` compatibility is isolated behind Platform/editor bridge code and the runtime API uses opaque/native-neutral descriptors plus UTF-8 commands.)*
- [ ] The CMake target graph mirrors these dependency boundaries; a generic target must not gain a backend/platform dependency merely to make one implementation compile.

---

## 4. Recommended third-party library policy

Use mature libraries for commodity work. Spend first-party engineering effort where Swim Engine gains architectural or performance value.

| Library | Purpose | Policy |
| --- | --- | --- |
| SDL3 | windows, event pump, native-window wrapping, controller, text input/IME, clipboard | Use behind `Swim::Platform` and `Swim::Input`. Do not expose `SDL_Window*` as normal public API. |
| EnTT | ECS | Keep. It already fits the engine well. |
| GLM | vector/matrix/quaternion math | Keep as the baseline math library unless a measured limitation appears; isolate API-specific projection conventions above/below generic math rather than forking math types per renderer. |
| nlohmann/json | human-readable scene/tool/config interchange | Keep for tooling and editable metadata where convenient; do not use JSON as the hot-path compiled asset representation. |
| enkiTS | general task scheduler | Recommended implementation for `Swim::Jobs`; replace renderer-only global thread pool. |
| mimalloc | general CPU heap / backing for focused transient arenas | Use as the process allocator and behind `Swim::Memory`; keep frame/scratch lifetime APIs engine-owned rather than exposing mimalloc as the gameplay allocation API. |
| fastgltf | glTF/GLB structure + extension metadata import | Compiler/dev-import only. It parses glTF and exposes extension metadata; it is **not** treated as the codec implementation for Draco, WebP, or Basis payloads. Never make it a shipping runtime dependency for compiled assets. |
| Draco | `KHR_draco_mesh_compression` geometry decode | Compiler/import only. Decode compressed primitives to ordinary Swim intermediate vertex/index data, then run meshoptimizer/cooking. Draco must not be linked by the shipping runtime. |
| libwebp | `EXT_texture_webp` image decode | Compiler/import only. Decode authoring WebP to compiler image data, then cook normal TextureAssets. WebP must not be a shipping runtime texture dependency. |
| Basis Universal transcoder | Basis/KTX2 universal texture transcode | Runtime use is allowed only while cooked KTX2/Basis payloads are intentionally platform-neutral. Keep the dependency behind texture residency; remove it from runtime once platform-native texture variants make runtime transcoding unnecessary. Encoder/tool code stays compiler-side. |
| meshoptimizer | vertex/index optimization, LOD, meshlets | Use offline in asset compiler. |
| KTX-Software/libktx | KTX2 texture processing/transcoding | Add/use when compiler-side KTX2 production needs it; runtime should consume Swim TextureAsset metadata/payloads rather than expose libktx types. |
| zstd | package/chunk compression | Keep behind asset/package code. |
| Slang | shader language/compiler/reflection | Canonical source/compiler path for all first-party shaders; emit backend artifacts and reflection at build/cook time. |
| Vulkan-Headers | Vulkan API definitions | Use in Vulkan backend only. |
| volk | Vulkan dispatch loading | Use in Vulkan backend. |
| vk-bootstrap | instance/device/queue/swapchain bootstrap | Use for boilerplate, while Swim owns feature and adapter policy. |
| Vulkan Memory Allocator | Vulkan allocation/suballocation/budgeting | Use. Do not maintain a home-grown general Vulkan allocator. |
| PhysX | physics backend | Keep as a selectable implementation. |
| Jolt Physics | physics backend | Use as an equal selectable implementation behind `Swim::PhysicsJolt`. |
| miniaudio | audio device/mixing/streaming/spatial playback | Use behind Swim Audio. |
| FreeType | font faces/metrics/raster data | Use. |
| HarfBuzz | Unicode shaping | Use. |
| msdfgen / msdf-atlas-gen | MSDF font atlas generation | Use for text assets/runtime atlas tooling. |
| stb | narrow source-import/image utility tasks | Keep narrowly scoped, preferably tool-side. |
| Tracy | CPU/GPU profiling | Integrate early enough that major renderer work is measurable. |

### 4.1 Library wrapper rule

Do not write fake abstractions around every dependency simply because it is third-party.

Wrap a library when at least one is true:

1. its types would otherwise leak through a stable engine API;
2. it represents a replaceable backend;
3. it owns platform-specific resources;
4. engine lifetime/error/threading policy must be imposed on it;
5. it is undesirable as a transitive dependency for engine users.

Examples:

- SDL3: wrap.
- PhysX/Jolt: wrap strongly.
- enkiTS: expose a Swim jobs API and keep enkiTS implementation details private.
- mimalloc: use as the process/general heap and arena backing, but expose Swim-owned lifetime concepts (`FrameArena`, scratch scopes) rather than allocator-specific APIs through gameplay/renderer contracts.
- fastgltf: tool/import boundary, no runtime wrapper object graph needed.
- meshoptimizer: call directly in the compiler implementation.
- VMA: keep inside Vulkan backend, not behind an additional allocator abstraction unless required by RHI internals.

### 4.2 Source codec ownership rule

Compressed/encoded **authoring support must not be confused with runtime dependency ownership**. Swim should accept common source formats aggressively while normalizing them at the compiler boundary.

```text
glTF/GLB structure          fastgltf
KHR_draco_mesh_compression  -> Draco decoder -> Swim intermediate mesh -> meshoptimizer -> MeshAsset
EXT_texture_webp            -> libwebp       -> compiler image/mips      -> TextureAsset
KHR_texture_basisu / KTX2   -> KTX2/Basis compiler path                  -> TextureAsset
PNG/JPEG                    -> stb_image     -> compiler image/mips      -> TextureAsset
```

Rules:

- [x] fastgltf owns glTF parsing/extension discovery only; it is not expected to replace dedicated compression/image codecs.
- [x] Draco decoding is compiler-only and successful `KHR_draco_mesh_compression` sources produce ordinary Swim mesh data before runtime serialization. *(Pinned Draco 1.5.7 is consumed through the private `Swim::AssetCompilerDependencies` bundle; `GltfImporter.cpp` translates decoded points/faces/attributes into Swim-owned `SourcePrimitive` data before meshoptimizer/static-model cooking.)*
- [x] WebP decoding is compiler-only; runtime assets do not remember WebP as an image-decoder requirement.
- [x] Basis Universal is an explicit exception only for **runtime transcoding of intentionally universal KTX2/Basis payloads**. It is not a loose-source model importer dependency.
- [ ] When platform-native cooked texture variants are authoritative, remove the Basis transcoder from the runtime target and leave Basis/KTX2 encoding/transcoding in tools only.
- [x] Codec/parser types never escape `SwimAssetCompiler` or renderer-residency implementation boundaries into gameplay/scene/public asset schemas.
- [x] The CMake dependency graph and the table below make every current third-party dependency's owner and runtime/compiler status obvious; no codec is linked to the normal runtime graph merely because a source asset may use its format. *(Compiler-only parser/optimizer/codecs are grouped behind `Swim::AssetCompilerDependencies`; development auto-cook is the explicit opt-in exception that links the compiler into the executable.)*

#### Current dependency ownership matrix

| Dependency | Pin / source | Owning target/layer | Shipping runtime? | Notes |
| --- | --- | --- | --- | --- |
| SDL3 | `release-3.4.14` | `Swim::Platform`, `Swim::Input` | yes | Private implementation dependency for platform/input. |
| mimalloc | `v3.4.5` | process allocator / `Swim::Memory` | yes | Final executable owns allocator override. |
| enkiTS | `v1.12` | `Swim::Jobs` | yes | Private scheduler implementation. |
| simdjson | `v3.12.3` | fastgltf implementation dependency inside `Swim::AssetCompilerDependencies` | no* | Established before fastgltf so fastgltf never mutates its cached checkout with fallback downloads. |
| fastgltf | `v0.9.0` | `Swim::AssetCompilerDependencies` -> `SwimAssetCompiler` | no* | glTF/GLB structure and extension metadata only. `*` Dev auto-cook may link the compiler into a development executable. |
| Draco | `1.5.7` | `Swim::AssetCompilerDraco` -> `Swim::AssetCompilerDependencies` -> `SwimAssetCompiler` | no* | Compiler-side `KHR_draco_mesh_compression` mesh decode only. The Swim adapter owns Draco 1.5.7's source/generated include-root quirk (`<source>/src` plus generated `draco_features.h`) so consumers never depend on package layout directly; glTF bitstream mode is enabled and unrelated point-cloud/tool/plugin builds are disabled. |
| meshoptimizer | `v1.1` | `Swim::AssetCompilerDependencies` -> `SwimAssetCompiler` | no* | Offline vertex/index optimization. |
| libwebp | `v1.5.0` | `Swim::AssetCompilerDependencies` -> `SwimAssetCompiler` | no* | Source `EXT_texture_webp` decode only; command-line utilities/mux/extras are disabled. |
| stb | commit `2dfbe86` | compiler image decode; legacy loose texture/font compatibility | transitional | Compiler handles PNG/JPEG source decode. Runtime stb remains only for compatibility paths not yet moved to compiled assets. |
| Basis Universal transcoder | `v1_60_snapshot_final` | `Swim::BasisTranscoder` in legacy/runtime texture residency | yes, intentional transitional | Only the transcoder TU is built; encoder/tools are omitted. Remove from runtime once platform-native cooked texture variants are authoritative. |
| zstd | `v1.4.9` | runtime asset/package + `Swim::BasisTranscoder` | yes | Runtime `.sasset`/texture decompression and universal KTX2 support. |
| spdlog | `v1.15.3` | process logging | yes | Static console + timestamped file logging. |
| GLM | `1.0.0` | engine math | yes | Header-only target. |
| EnTT | `v3.13.2` | scene/ECS implementation | yes | Header-only target; backend types must not leak into ECS contracts. |
| nlohmann/json | `3.10.4` single header + SHA-256 | editable config/tool/legacy scene interchange | yes while those paths use it | Verified release header avoids Windows path/cache issues. |
| GLAD | `v2.0.8` generated loader | legacy OpenGL backend | yes while legacy backend ships | Must eventually live only in the OpenGL implementation target. |
| OpenGL | Windows/system API | legacy OpenGL backend | yes while legacy backend ships | No source-import responsibility. |
| Vulkan-Headers | system Vulkan SDK | `Swim::RhiVulkan` | yes | Header/API definitions are private to the Vulkan implementation target. The modern backend does not link the Vulkan loader directly. |
| volk | `1.4.350` | `Swim::RhiVulkan` | yes | Namespaced static meta-loader/dispatch tables so the modern backend can coexist temporarily with the legacy directly-linked Vulkan path without symbol collisions. |
| vk-bootstrap | `v1.4.350` | `Swim::RhiVulkan` | yes | Instance, physical-device, logical-device, and queue bootstrap only; Swim still owns required feature/adapter policy. |
| PhysX | `107.3-omni-and-physx-5.6.1` | `Swim::PhysicsPhysX` | yes | External CPU-only backend; implementation types stay private to the backend target. |
| Jolt Physics | `v5.6.0` | `Swim::PhysicsJolt` | yes | CPM source build, static library, single-precision positions; Jolt compute/GPU/sample/tool paths are disabled and implementation types stay private to the backend target. |
| FreeType | `VER-2-14-3` | `Swim::TextDependencies` -> text/UI module (item 79) | yes, once the text module ships | Font faces, metrics and outlines. zlib/bzip2/PNG/Brotli/HarfBuzz integration disabled. |
| HarfBuzz | `14.5.0` single-file `src/harfbuzz.cc` | `SwimHarfBuzz` -> `Swim::TextDependencies` | yes, once the text module ships | Unicode shaping with built-in Unicode data; no FreeType/ICU/GLib interop. |
| msdfgen | `v1.13` core | `Swim::TextDependencies` | yes, once the text module ships | Glyph MSDF generation from Swim-built shapes. msdf-atlas-gen is not pinned: atlas packing is planned in Swim; revisit if that proves insufficient. |
| Slang compiler SDK | `2026.16.1` official release ZIP + SHA-256 | `SwimSlangCompiler` build tool -> `Swim::ShaderCompiler` metadata tooling | no | Build-only `slangc`; emits SPIR-V, reflection JSON, and depfiles. Slang implementation libraries/types are not linked into runtime or exposed by public shader metadata headers. |

`no*` means the normal compiled-asset runtime does not need the dependency; a development build with in-process auto-cooking may intentionally include the asset compiler.

Build/tool dependencies are separate from linked engine libraries: CMake requires `3.25+`; CPM.cmake is pinned to `0.40.8`; Git is used for immutable dependency-cache validation and PhysX worktree setup; Python 3 is required by GLAD/PhysX upstream generation; Slang `2026.16.1` is fetched as a hash-verified compiler SDK for deterministic `.slang` -> SPIR-V/GLSL/reflection builds. The old first-party DXC/HLSL shader path is retired. These tools do not become runtime library dependencies.

### 4.3 Build graph and CMake invariants

CMake is the authoritative build description and should reinforce the engine architecture rather than merely collect source files. It is infrastructure used throughout every phase, not a separate feature milestone.

- [ ] First-party subsystems have explicit targets with intentional public/private dependency edges.
- [ ] Third-party libraries are linked only by the implementation target that owns them.
- [x] `Swim::Rhi` does not link Vulkan; `Swim::RhiVulkan` privately owns Vulkan-Headers, volk, vk-bootstrap, and VMA. *(The generic target remains dependency-free; allocator implementation types never cross the RHI contract.)*
- [x] `Swim::Physics` does not link PhysX or Jolt directly; `Swim::PhysicsPhysX` and `Swim::PhysicsJolt` own their implementation dependencies privately.
- [x] `Swim::PhysicsJolt` owns Jolt privately through `Swim::Jolt`; generic/runtime consumers link the Swim backend target rather than raw `Jolt::Jolt`.
- [x] `Swim::Platform` / `Swim::Input` own SDL3 integration rather than making SDL3 an engine-wide dependency.
- [x] fastgltf, meshoptimizer, Draco, source-image decoders, and similar importer dependencies live in asset compiler/import targets unless runtime use is explicitly required. *(fastgltf/meshoptimizer and libwebp source-image decoding are compiler-only; the obsolete runtime tinygltf/Draco/WebP source-import chain has been removed. `stb` is compiler-only for PNG/JPEG authoring decode and remains an explicit legacy-runtime dependency only for still-loose texture/font compatibility paths.)*
- [x] Slang shader compilation/reflection is integrated through dedicated build/tool rules with correct source/include dependency tracking. *(The pinned `slangc` rule declares CMake `OUTPUT`s and consumes Slang's generated depfile; reflection is parsed behind the Swim-owned `ShaderReflection` contract.)*
- [ ] Generated shader artifacts and compiled asset outputs have deterministic build dependencies and are not maintained by ad-hoc post-build shell scripts.
- [ ] Platform-specific source files and libraries are selected inside the appropriate implementation targets; generic code does not accumulate broad `#ifdef _WIN32` / Linux branches.
- [x] No machine-specific absolute include/library paths.
- [ ] Third-party compiler options and warnings do not leak into first-party targets.
- [x] Tests and examples compile/link the same first-party module code that real applications are expected to consume.
- [x] Visual Studio solution organization reflects ownership without inventing duplicate source ownership. *(Superseded 2026-09-05: `SwimEngine` stays at the root and now directly contains every former "Engine Modules" source list as `source_group(TREE ...)` filters mirroring `Source/...` on disk — see the "Engine module collapse checkpoint" below and `docs/VisualStudioProjectStructure.md` §11. Tests/Tools/Examples/Third Party/CMake solution folders are unchanged.)*
- [x] Tests/examples are `EXCLUDE_FROM_ALL`, so validation/demo targets remain explicitly buildable without bloating the normal engine build.
- [x] Windows clean and soft workflows both regenerate and validate `build/windows-vs/SwimEngine.sln`; the soft path refreshes it with dependency fetching fully disconnected while the actual iterative compile remains on the Ninja `windows-release`/`windows-debug` tree.
- [x] `SwimPlatform` / `SwimInput` sources (and every other former "Engine Modules" source list) compile exactly once per final binary. *(Superseded 2026-09-05: no longer via a shared module target linked by each consumer — each consumer that needs a module's sources now compiles them directly, with the one exception being Assets, which compiles into `SwimAssetCompiler` instead of `SwimEngine`/`SwimTests` whenever that target is linked, to avoid a duplicate-symbol link error. See the checkpoint below.)*
- [x] Optional implementation backends are compile-time capabilities; the selected implementation is a runtime choice among the backends that were compiled in. *(PhysX and Jolt can coexist in one Windows build; Jolt also builds in the Linux foundation configuration.)*

**Build workflow checkpoint (2026-09-02, hardened 2026-09-03):** SDL3 is a pinned CPM dependency (`libsdl-org/SDL`, `release-3.4.14`) owned privately by `Swim::Platform`. CPM sources are cached under `.cache/cpm`. A clean build is now defined as a full repository-local generated-state reset rather than merely deleting the selected target: Windows clean removes `build/windows-release`, `build/windows-debug`, `build/windows-vs`, the shared `build/.px` PhysX worktree/legacy junction, and the entire `.cache` tree; Linux clean removes both Linux configuration trees, `build/.px`, and `.cache`. Both scripts verify that the generated state is actually absent before fetching. `scripts/build-windows-soft.ps1` and `scripts/build-linux-soft.sh` configure with `FETCHCONTENT_FULLY_DISCONNECTED=ON`, so iterative rebuilds are restricted to already-cached dependency sources. Windows and Linux Debug/Release Ninja presets exist for those scripts. The legacy `scripts/build-windows.ps1` now forwards to the soft-build path. Matching `.bat` launchers exist for all four Windows/Linux clean/soft workflows; the Linux launchers run the Bash scripts through WSL, and every launcher preserves the build exit code and pauses before closing so one-click builds remain readable. Windows builds are also self-bootstrapping with respect to the local toolchain environment: `scripts/windows-build-common.ps1` discovers CMake from PATH or Visual Studio, finds Visual Studio/Build Tools through `vswhere` or standard install locations, imports the x64 MSVC environment when Ninja is selected, discovers Visual Studio's bundled Ninja even when it is not on PATH, and falls back to the Visual Studio 2022 generator when Ninja is unavailable. The helper uses `DebugBuild` internally to avoid colliding with PowerShell's built-in common `Debug` parameter while the public scripts continue to accept `-Debug`. Every clean Windows run also recreates `build/windows-vs/SwimEngine.sln`; when Ninja is the primary compile generator, the solution configure is performed only after the primary Ninja build completes, using the freshly populated and integrity-checked dependency cache with dependency downloads disabled. Windows soft builds follow the same build-first ordering and perform the Visual Studio solution configure in fully disconnected mode on every successful run, so `build/windows-vs/SwimEngine.sln` stays synchronized without placing a second-generator configure between Ninja configure and compilation. Git-backed dependency caches are audited as immutable inputs during configure, and PhysX now builds from a short detached Git worktree at `build/.px` rather than a junction into the CPM checkout, so NVIDIA-generated compiler/bin output can no longer dirty the cached PhysX source. This removes the previous requirement to launch builds from a Developer Command Prompt or install Ninja separately while ensuring a normal Visual Studio solution is always available and synchronized after either Windows build workflow.


**Visual Studio solution hygiene checkpoint (2026-09-03):** The CMake target graph remains modular, but the generated solution is no longer allowed to expose that entire graph as a flat list. `USE_FOLDERS` and `PREDEFINED_TARGETS_FOLDER` are enabled centrally in `cmake/SolutionLayout.cmake`. The primary `SwimEngine` executable stays at the solution root; the reusable first-party `SwimPlatform` and `SwimInput` targets live under `Engine Modules`; validation executables/object targets live under `Tests`; smoke/demo executables live under `Examples`; generated CMake projects live under `CMake`; and dependency projects live under `Third Party`, with the large SDL3, Draco, WebP, GLAD, PhysX, zstd, and Basis graphs grouped by dependency where applicable. This is presentation-only and does not duplicate source ownership: Platform/Input sources remain excluded from the `SwimEngine` source glob and are linked exactly once through their module libraries. Tests/examples are now `EXCLUDE_FROM_ALL`, so they remain explicitly buildable from Visual Studio without participating in the normal engine build. Both Windows clean and soft scripts compile the primary Ninja tree first, then regenerate the Visual Studio solution and validate that the required solution folders exist; the standalone solution-generation script uses the same toolchain discovery and validation path. *(Superseded by the engine module collapse checkpoint below — the "Engine Modules" solution folder and its per-module targets no longer exist; this entry is kept for history.)*

**Engine module collapse checkpoint (2026-09-05):** The "Engine Modules" solution folder (and its `RHI Backends`/`Physics Backends` subfolders) is gone. Every target that used to live there — `SwimCore`, `SwimMemory`, `SwimJobs`, `SwimPlatform`, `SwimInput`, `SwimCommands`, `SwimIO`, `SwimAssets`, `SwimPhysics`, `SwimPhysicsJolt`, `SwimPhysicsPhysX`, `SwimRhi`, `SwimRhiVulkan` — was retired as a CMake target. Their `file(GLOB_RECURSE ...)` source discovery, their third-party dependency setup, and their feature gating (`SWIM_ENABLE_JOLT_BACKEND`, `SWIM_ENABLE_PHYSX_BACKEND`, `SWIM_ENABLE_VULKAN_RHI`) all still happen at exactly the same points in `CMakeLists.txt`, but the sources now compile directly into whichever real binary needs them — almost always `SwimEngine` — and show up there as `source_group(TREE ...)` filters mirroring `Source/...` on disk, the same mechanism `SwimEngine` already used for its own unsplit sources. This directly satisfies §0.2 (no monolithic implementation files) at the *source-file* level while changing the *CMake-target* level: §0.2 is about how a module's own files are organized (declarations vs. definitions, one clear owner per file); this checkpoint is about which target compiles those files, a distinct and compatible concern.

Two rules kept the collapse safe against duplicate-symbol link errors: (1) a module's sources compile into at most one place within any given final binary — where a binary would otherwise get a module two ways at once (`SwimTests` links `SwimAssetCompiler`, which now embeds Assets' sources itself, while also wanting Assets directly), exactly one path actually compiles the sources and the other links that target instead; `SwimEngine` has the identical case with development auto-cook (`SWIM_ENABLE_DEV_ASSET_AUTOCOOK`); (2) a module's own third-party `PRIVATE` dependency (SDL3, mimalloc, enkiTS, Jolt, PhysX, volk/vk-bootstrap/VulkanMemoryAllocator) moved onto whichever target now compiles it, exactly mirroring what the retired module target used to declare.

`Tests`, `Third Party`, `Tools`, and `Examples` solution folders are unchanged — this was scoped to "Engine Modules" only. The one adjustment inside `Tests` was to the header-boundary gates (`Tests/Header Boundary`): most no longer need a `LINK` argument, since the module aliases they used to name only ever provided the default `${CMAKE_SOURCE_DIR}/Source` include path (already set unconditionally) and an `OBJECT` library never actually links; the two physics gates that needed a real third-party include path now link `glm::glm` directly instead of the retired `Swim::Physics` alias. `SwimAssetCompiler` (Tools) picked up one new responsibility: it now embeds `Source/Engine/Assets/...` directly, since `SwimAssets` no longer exists for it to link. Full rationale and the per-target consumer list: `docs/VisualStudioProjectStructure.md` §11.

**Ninja manifest stability checkpoint (2026-09-03):** Windows soft/clean workflows keep the primary Ninja configure and build contiguous and refresh the secondary Visual Studio solution only after a successful primary build. First-party and shader source discovery remains configure-time globbing but no longer uses `CONFIGURE_DEPENDS`. A real clean Windows run proved that removing first-party glob watching was not sufficient because a fetched third-party project can still add its own `VerifyGlobs` edge; the resulting Ninja manifest repeatedly ran CMake (`[0/2]`, `[0/4]`, `[0/6]`, ...) without ever reaching compilation. Swim's Ninja presets now set `CMAKE_SUPPRESS_REGENERATION=ON`, and the top-level project forces the same contract for every Ninja generator. This is safe because every supported clean/soft workflow explicitly configures immediately before building, so source/dependency graph changes are still discovered before compilation while dependency-owned automatic regeneration cannot trap Ninja in a manifest loop. Both Windows scripts call `Assert-SwimNinjaManifestStable` after configure and refuse to invoke Ninja if `RERUN_CMAKE`, `VerifyGlobs.cmake`, or `cmake.verify_globs` appears in `build.ninja`. Configure-time generated Basis transcoder source remains write-if-different so unchanged configuration also preserves its timestamp. `verify-build-layout.py` guards these invariants.


**Current foundation/build status (2026-09-03):** dependency/bootstrap hardening remains in place: clean deletion is idempotent and long-path aware, the CPM cache is integrity-checked, nlohmann/json uses a pinned verified single-header artifact, PhysX builds from the short detached `build/.px` worktree, enkiTS v1.12 is pinned/cached for `Swim::Jobs`, mimalloc v3.4.5 is pinned for the process allocator/`Swim::Memory`, and the solution is regenerated by both Windows workflows. A real Windows clean build previously exposed an over-broad legacy source exclusion: excluding every path containing `/IO/` also removed `Source/Engine/Systems/IO/CommandSystem.cpp` and `InputManager.cpp`, producing unresolved CommandSystem/InputManager symbols. The exclusion rules are now anchored to exact first-party module roots and the verifier rejects broad exclusions. **The corrected clean Windows/MSVC build now succeeds with mimalloc fetched, the static allocator override linked, and the legacy executable completing its final link.** Normal C++ iteration returns to the soft Windows build path; future clean builds are required only when dependency declarations/pins change.

```text
Build-time availability
    Vulkan backend: enabled
    OpenGL legacy backend: enabled
    PhysX backend: enabled
    Jolt backend: enabled

Runtime selection
    GraphicsBackend::Vulkan
    PhysicsBackend::PhysX / PhysicsBackend::Jolt
```

A useful module dependency shape is shown below. `A -> B` means **A depends on B**. This is a *code-ownership* graph, not necessarily a CMake link-target graph any more: since the 2026-09-05 engine module collapse (see the checkpoint above and `docs/VisualStudioProjectStructure.md` §11), Physics, PhysicsPhysX, PhysicsJolt, Rhi, RhiVulkan, Assets, Input, Io, Jobs, Memory, and Platform are directory/namespace boundaries enforced by code review and `source_group` filters, compiled directly into `SwimEngine` (or `SwimTests`/an example, when that binary needs the same code) rather than separate CMake targets each consumer links. `Swim::Engine`, `Swim::Render`, `Swim::Scene`, `Swim::Animation`, `Swim::Audio`, and `Swim::Ui` remain future extraction targets, named here as aspirational module boundaries, not current CMake targets:

```text
Swim::Engine      -> Render, Scene, Physics, Animation, Audio, Ui, Input, Assets, Jobs, Io, Memory, Platform, Core
Swim::Render      -> Rhi, Assets, Jobs, Core
Swim::RhiVulkan   -> Rhi, Platform, Core, Vulkan-Headers, volk, vk-bootstrap, VMA
Swim::Rhi         -> Core
Swim::PhysicsPhysX-> Physics, PhysX
Swim::PhysicsJolt -> Physics, Jolt
Swim::Physics     -> Core
Swim::Scene       -> Assets, Jobs, Core, EnTT
Swim::Assets      -> Io, Jobs, Platform, Core
Swim::Input       -> Platform, Core
Swim::Io          -> Platform, Jobs, Core
Swim::Jobs        -> Memory, Core, enkiTS
Swim::Memory      -> Core, mimalloc
Swim::Platform    -> Core, SDL3
```

Asset compiler/import targets may additionally depend on fastgltf, meshoptimizer, KTX tooling, Draco, and source-image libraries without making those dependencies part of the normal runtime graph.

The graph is also a diagnostic tool. If `Swim::Render` suddenly needs to link SDL3, PhysX, Jolt, or Vulkan directly, or if gameplay needs fastgltf, that should be treated as a likely architecture violation rather than normal dependency growth.

---

# Part II — Implementation phases in dependency order

## Phase 1 — Cross-platform platform layer

**This phase is the first implementation phase because every higher-level runtime system consumes these contracts. Do not start the modern RHI/renderer, authoritative asset runtime, expanded gameplay input, or multi-backend physics architecture on temporary OS-specific foundations.**

The goal is to create the stable host environment everything else consumes: window/native-handle abstraction, events, input, filesystem/path services, and the platform hooks required by rendering, IO, tools, and application lifetime.

### Platform module responsibilities

Create a `Swim::Platform` module with these public concepts:

- [x] `PlatformSystem`
- [x] `WindowSystem`
- [x] `Window`
- [x] `WindowId`
- [x] `WindowDesc`
- [x] `WindowEvent`
- [x] `NativeWindowHandle` escape hatch
- [x] `DisplayInfo`
- [x] `FileSystem`
- [x] `MappedFile`
- [x] `DynamicLibrary`
- [x] `MonotonicClock`
- [x] thread naming/affinity helpers where needed
- [x] executable/base/pref/cache paths
- [x] headless mode

SDL3 should implement window creation/event pumping and provide the bridge to platform-native handles.

### Window API

A generic window API should expose information, not OS objects:

```cpp
struct WindowDesc
{
    std::string Title = "Swim";
    uint32_t Width = 1280;
    uint32_t Height = 720;
    bool Resizable = true;
    bool HighPixelDensity = true;
};

class Window
{
public:
    WindowId GetId() const;
    Extent2D GetLogicalSize() const;
    Extent2D GetPixelSize() const;
    float GetDpiScale() const;
    bool IsFocused() const;
    bool IsMinimized() const;

    NativeWindowHandle GetNativeHandle() const;
};
```

The RHI should normally consume a `Window&`/presentation-source contract, not an `HWND`.

For Vulkan, the SDL-backed platform implementation can create the Vulkan surface through SDL's Vulkan WSI helpers without exposing the OS-native window to high-level renderer code.

### Embedded/external editor windows

Do not lose the ability to host the engine inside an external editor window.

- [x] Platform window creation supports wrapping an existing native window when the platform implementation supports it.
- [x] On Windows, SDL3 can wrap an existing `HWND` through window properties.
- [x] The generic engine API receives an opaque external-window descriptor rather than an `HWND` field.
- [x] Windows-specific focus/parenting work remains inside the Windows/SDL platform adapter.
- [x] Editor IPC is not part of `Window` itself.

### Event model

Create one normalized event stream:

```text
SDL / OS event
    -> Platform normalized event
        -> Window state
        -> InputSystem
        -> UI event routing
        -> optional application callbacks
```

Renderer code should react to high-level resize/minimize/display events rather than Win32 messages.

### Filesystem/path policy

- [x] UTF-8 engine path convention.
- [x] Use `std::filesystem::path` internally where appropriate.
- [x] `GetExecutableDirectory()` becomes a platform service rather than a Win32 call in `SwimEngine`.
- [x] Define application asset root, writable user-data root, cache root, temporary root.
- [x] No hard-coded `Assets\\Textures` or executable-relative assumptions inside asset classes.
- [x] Provide memory-mapped file support for runtime asset packages.
- [x] Keep path resolution distinct from AssetId identity.

### Input abstraction belongs in the same foundation wave

Gameplay must stop consuming virtual-key numbers before higher-level gameplay/UI work expands.

Create:

- [x] `KeyCode`
- [x] physical `ScanCode`
- [x] `MouseButton`
- [x] `GamepadButton`
- [x] `GamepadAxis`
- [x] `InputDeviceId`
- [x] `InputAction`
- [x] `InputMap`
- [x] text input events
- [x] IME composition events
- [x] pointer/mouse motion
- [x] wheel events
- [x] controller hotplug
- [x] rumble/haptics where supported
- [x] focus-aware input state
- [x] event API and sampled-state API

Do not encode UI scaling corrections or title-bar hacks in `InputSystem`. UI converts physical/logical window coordinates through its own canvas/layout system.

### PCH cleanup

The generic PCH must not be a hidden platform/backend dependency injector.

- [x] Remove `Windows.h` from generic PCH.
- [x] Remove Vulkan headers from generic PCH.
- [x] Remove GL/WGL headers from generic PCH.
- [x] Backend implementation translation units include what they use.
- [x] Platform implementation translation units include native headers privately.

### Phase 1 checkpoint status — 2026-09-02

Implemented in this checkpoint:

- `Swim::Platform` is a standalone first-party target and SDL3 is private to it; SDL/native headers are absent from the public platform/input headers.
- `Swim::Input` is a standalone first-party target over normalized platform events. Its initial migration used an `InputManager` adapter; the 2026-09-05 retirement checkpoint removes that adapter from all active consumers (see §0.3).
- Window, display, input, filesystem roots, memory mapping, dynamic-library loading, monotonic time, thread naming/affinity, headless startup, external-window descriptors, and the Windows editor-host compatibility bridge are present.
- `SwimEngine` no longer owns a Win32 window procedure/message pump contract or exposes `HWND` as its window abstraction. Editor `WM_COPYDATA`, parenting, dialog-key behavior, and focus compatibility are isolated in the Windows platform implementation.
- The generic PCH no longer injects Windows, Vulkan, OpenGL/WGL, or SDL headers. UI coordinate conversion was removed from input and kept in UI/render-space code.
- `HelloWindow`, `HeadlessPlatform`, platform public-header compile coverage, and standalone input-state tests were added.
- CMake has Windows/Linux Debug and Release presets plus clean/soft build entrypoints. Clean builds repull CPM dependencies; soft builds are disconnected and reuse `.cache/cpm`. One-click `.bat` wrappers are available for every workflow and keep the console open after completion.

Validation completed in the checkpoint environment:

- `scripts/verify-build-layout.py` passes with architecture-boundary and clean/soft build-workflow checks.
- The platform public headers compile on Linux without SDL/native SDK headers.
- Standalone input tests pass for key press/trigger/release, mouse motion, gamepad axes/hotplug, action maps, resize state, and focus loss.
- Offline CMake configuration for the platform/foundation layout succeeds.

### Windows compile follow-up — 2026-09-03

A real clean Windows build has now passed the dependency/toolchain/configuration boundary. The clean script discovered Visual Studio 2022/MSVC, standalone CMake, Visual Studio's bundled Ninja, downloaded the pinned CPM dependency set, configured SDL3 3.4.14, found the Vulkan SDK, generated the Ninja build tree, and generated `build/windows-vs/SwimEngine.sln` from the same dependency cache. The first failure occurred only after compilation entered first-party `SwimPlatform` sources.

Fixes made from that compiler checkpoint:

- [x] Rename `WindowSystem::CreateWindow` to `WindowSystem::Create`. `CreateWindow` is a Win32 function-like macro, so merely undefining it in one implementation file would leave the public C++ API vulnerable in any caller that includes `Windows.h`.
- [x] Update `DynamicLibrary` for SDL3's typed `SDL_SharedObject*` API and expose an implementation-neutral function-pointer result rather than forcing SDL's function pointer through an object-pointer return type.
- [x] Replace deprecated C++20 `std::filesystem::u8path` calls with explicit UTF-8 byte-preserving `std::u8string` path construction.
- [x] Enable process-local Git `core.longpaths=true` in the Windows build bootstrap so CPM clones do not dirty dependency checkouts when a dependency contains paths beyond the traditional Windows path limit.
- [x] Trim SDL's build to the Platform-owned facilities actually needed by the engine. SDL audio, camera, SDL_GPU, SDL_Renderer, sensor, dialog, and tray subsystems are disabled; video/events/gamepad/haptic support remains available.
- [x] Correct `Platform/Internal/WindowInternal.h` to include `Engine/Platform/Window.h` through the project include root. MSVC tolerated the previous nested bare `"Window.h"` lookup, but GCC/Clang do not reliably search the including source file's parent directory for a nested header's quoted include.
- [x] Extend build-layout verification so the Win32 `CreateWindow` naming regression, internal include-root contract, Git long-path bootstrap, and minimal SDL subsystem contract are checked automatically.
- [x] Re-run local foundation validation after the fixes: build-layout verification, public Platform header compilation, standalone Input tests, offline CMake foundation configuration, and Windows/SDL-shaped Platform syntax compilation all pass.

The next real Windows build did proceed past the `DynamicLibrary.cpp`, `WindowSystem.cpp`, and `FileSystem.cpp` issues. Compilation reached the external PhysX build at roughly 436/676 targets, where PhysX's generated Visual Studio compiler probe failed because NVIDIA's default source-relative `physx/compiler/vc17win64-cpu-only/CMakeFiles/CMakeScratch/...` path was rooted underneath the already-long CPM cache path. MSBuild rejected its `.lastbuildstate` path for exceeding the traditional 260-character limit. This is a build-path problem, not a compiler/dependency-version failure.

PhysX path-length follow-up:

- [x] Keep the authoritative PhysX checkout in CPM rather than copying or vendoring another source tree.
- [x] Give PhysX one stable short detached Git worktree at `build/.px`, shared by both the Ninja and generated Visual Studio build trees. NVIDIA's generator sees that short worktree as its source root, so its source-relative `compiler/vc17win64-cpu-only` build tree and MSBuild scratch/tlog paths no longer inherit CPM's hash-heavy directory depth, while generated PhysX files never modify the authoritative CPM checkout.
- [x] Recreate/validate the short worktree from `BuildPhysX.cmake` before running NVIDIA's generator; stale legacy junctions/worktrees are replaced rather than reused against a different dependency checkout.
- [x] Detect an existing PhysX `CMakeCache.txt` that was generated through the old long CPM path and remove only that generated compiler tree before regenerating through the short alias. Applying this checkpoint does not require manually hunting down the stale PhysX cache.
- [x] Preserve the existing PhysX ABI contract: pinned 107.3 / PhysX 5.6.1 source, CPU-only VS2022 preset, static libraries, static non-debug CRT, Checked for engine Debug/RelWithDebInfo, and Release for engine Release.
- [x] Extend build-layout verification so removing the short PhysX path or silently reverting the external build to the long CPM path is caught.

The next clean Windows build verified that the short PhysX path works in the real toolchain: PhysX configured and compiled from `build/.px`, including Checked and Release libraries, and the overall build advanced to roughly 611/676 before entering the legacy engine PCH. The next failure was `nlohmann/json.hpp` missing from an include directory that CMake had correctly propagated. The same configure had already warned that CPM considered the `nlohmann_json_source` checkout dirty, so this was a corrupted/incomplete source-cache problem rather than a target-link/include-propagation mistake.

nlohmann/json cache-integrity follow-up:

- [x] Stop cloning the full nlohmann/json v3.10.4 repository. Swim Engine only includes `<nlohmann/json.hpp>`, and that repository contains historical report paths that are hostile to traditional Windows path handling.
- [x] Fetch the official v3.10.4 release `json.hpp` single-header artifact directly through CMake into the shared dependency cache instead of accepting a Git checkout.
- [x] Pin the official published SHA-256 (`c9ac7589260f36ea7016d4d51a6c95809803298c7caec9f55830a0214c5f9140`) and validate the cached header before exposing `nlohmann_json::nlohmann_json`. A corrupt/partial header now fails at configure time instead of while compiling the PCH.
- [x] Preserve soft-build offline semantics: an absent or invalid JSON header in disconnected mode reports that one clean build is required rather than silently accessing the network.
- [x] Extend build-layout verification so the full nlohmann Git checkout cannot accidentally be reintroduced and the version/hash/offline-integrity contract remains pinned.

Validation still required before declaring Phase 1 fully exited:

- Run the real SDL-backed `HelloWindow`/headless executables on both Windows and Linux. The current execution environment cannot resolve GitHub from the build container, so it cannot perform the fresh CPM/SDL pull needed for that test.
- [x] Complete the real Windows/MSVC build through the remaining first-party objects and final link. The latest soft-build checkpoint now succeeds; runtime launch/smoke coverage remains separate from compile/link validation.
- Explicitly build the `SwimPlatformPublicHeaders` validation target on Windows so the Windows half of the generic-public-header exit criterion is proven rather than inferred from the main engine compile.
- The headless Core/Jobs/Assets exit criterion remains open because Core and Jobs now exist, but the authoritative Assets runtime is Phase 4 work and has not been implemented yet.

**Clean-cache hardening:** real Windows clean runs exposed two independent deletion hazards: preserving `build/.px` while removing `.cache` could leave a short PhysX alias/worktree tied to a deleted checkout, and Windows PowerShell recursive deletion could fail halfway through old dependency caches containing paths beyond `MAX_PATH`, leaving a partially removed tree that then failed differently on the next run. Clean builds now remove all repository-local cache state and the short PhysX path before fetching. Windows cleanup is explicitly idempotent: already-absent paths and broken legacy junctions count as success, directory-entry existence is checked without requiring a junction target to resolve, and recursive deletion uses Windows extended-length (`\\?\`) paths through native `rd /s /q` rather than `Remove-Item -Recurse`. The post-delete verification uses the same directory-entry test, so a broken reparse point cannot be mistaken for a successful clean. PhysX builds from a detached Git worktree at `build/.px`, keeping generated NVIDIA projects/binaries isolated from the pinned CPM checkout. Configure-time dependency integrity checks fail immediately on any dirty Git-backed cache instead of allowing dirty-source state to break much later in PCH/compiler work.

**Windows renderer compile checkpoint:** after clean-state deletion, immutable dependency-cache setup, nlohmann single-header acquisition, and the short PhysX worktree all succeeded in the real MSVC build, the legacy engine advanced into renderer compilation (roughly 632/676). The large error burst was two Win32 include-boundary regressions exposed by removing `Windows.h` from the generic PCH: legacy `min`/`max` macros were leaking from renderer-local Windows headers and corrupting `std::min`, `std::max`, and `std::numeric_limits<T>::max()` expressions across OpenGL/Vulkan code; separately, the Vulkan Win32 surface declarations were missing because `VK_USE_PLATFORM_WIN32_KHR` was not guaranteed before `<vulkan/vulkan.h>`. The renderer now consumes a centralized internal `WindowsApi.h` wrapper that defines `WIN32_LEAN_AND_MEAN` and `NOMINMAX`, the legacy Windows target also carries those definitions defensively, and the Vulkan backend defines/enforces `VK_USE_PLATFORM_WIN32_KHR` before Vulkan headers. Build-layout verification now rejects raw renderer `Windows.h` includes and checks the Vulkan platform-define ordering.

**Windows scene/editor compile checkpoint:** the next real MSVC run confirmed the Win32 macro and Vulkan surface fixes and advanced to roughly 668/676 with only `Scene.cpp` failing. The remaining error was a stale pre-refactor editor-hotkey helper still forwarding `const wchar_t*` commands into the now platform-neutral UTF-8 `SwimEngine::OnEditorCommand(std::string_view)` API. Scene hotkeys now use narrow UTF-8 command literals through `std::string_view`, and verification rejects reintroducing the old wide-string command path. At this point the dependency cache and PhysX worktree have been established successfully by a true clean build; normal first-party C++ iteration should use the soft build unless dependency declarations, pins, or generated dependency-cache layout change.

### Phase 1 exit criteria

- [ ] A `HelloWindow` application runs on Windows and Linux from the same public API. *(Implementation exists; real SDL-backed runtime verification is still pending on both OSes.)*
- [x] window resize/minimize/focus/DPI events are normalized.
- [x] keyboard/mouse/controller APIs contain no Win32 key/message types.
- [x] a headless application can initialize Core/Jobs/Assets tests with no window. *(`SwimHeadlessCoreAssets` initializes `Swim::Jobs` + engine-owned `Swim::Assets`, declares/publishes/resolves an asset, and exits without creating Platform/window state.)*
- [ ] generic public headers compile on Windows and Linux without `Windows.h`. *(Linux compile is verified and the real Windows engine compile now gets deep into first-party code without generic Win32 leakage, but the dedicated `SwimPlatformPublicHeaders` target still needs an explicit Windows build before this is checked off.)*
- [x] existing editor-window embedding has a Windows compatibility path through Platform rather than `SwimEngine`.
- [x] Platform/Input implementation targets obey the CMake boundaries: SDL/native libraries do not leak into unrelated generic targets.

---

## Phase 2 — Engine ownership, lifecycle, and runtime backend selection

The engine must stop using global discovery before new systems are layered on top.

### Runtime configuration

Backend selection should be runtime data:

```cpp
enum class GraphicsBackend
{
    Auto,
    Vulkan,
    OpenGLLegacy,
    D3D12,
    Metal
};

enum class PhysicsBackend
{
    Auto,
    PhysX,
    Jolt
};

struct EngineConfig
{
    GraphicsBackend Graphics = GraphicsBackend::Vulkan;
    PhysicsBackend Physics = PhysicsBackend::PhysX;
    WindowDesc Window;
};
```

Command-line examples:

```text
--graphics=vulkan
--graphics=opengl
--physics=physx
--physics=jolt
```

Selection must happen before resources that depend on the choice are created.

### Replace the global engine service locator

`SwimEngine::GetInstance()` should not be the normal dependency path.

Preferred ownership model:

```text
Engine
  owns Platform
  owns Jobs
  owns IO
  owns Assets
  owns Input
  owns SceneManager
  owns PhysicsSystem
  owns RenderSystem
  owns Audio
  owns Debug services
```

Use unique ownership by default. Expose references/non-owning service views to consumers.

Example:

```cpp
struct EngineServices
{
    PlatformSystem& Platform;
    InputSystem& Input;
    JobSystem& Jobs;
    AssetSystem& Assets;
    SceneManager& Scenes;
    PhysicsSystem& Physics;
    RenderSystem& Render;
};
```

This is dependency injection, not a global singleton.

### Replace stringly typed core subsystem ownership

The current `SystemManager` preserves insertion order and stores `shared_ptr<Machine>` by string. That is convenient but makes dependency order implicit.

For core systems:

- [x] use typed members/owners;
- [x] make startup order explicit;
- [x] make shutdown order the reverse dependency order;
- [x] fail startup with structured diagnostics;
- [x] keep a dynamic subsystem registry only for optional/plugin systems that truly need it.

### Lifecycle model

The current `Awake/Init/Update/FixedUpdate/Exit` pattern may remain as a convenience where useful, but core services should not depend on every subsystem inheriting the same base class.

Define explicit runtime phases:

```text
Construct configuration
Create Platform
Create Jobs/IO
Create Asset services
Create Window/Input
Create selected Physics backend
Create selected Graphics backend
Create high-level renderer
Create scenes/game

Frame:
  Pump platform events
  Input begin frame
  Game/scene update
  Fixed-step simulation as required
  Render extraction
  Render
  Input end frame

Shutdown:
  Game/scenes
  Render
  Physics
  Assets/IO
  Window/Input
  Jobs
  Platform
```

### `shared_ptr` policy

`shared_ptr` should not be the default ownership mechanism for every engine service and asset.

Use:

- `unique_ptr` or direct members for unique system ownership;
- typed generational handles for assets and GPU objects;
- raw/reference non-owning pointers only where lifetime is guaranteed and documented;
- `shared_ptr` for genuinely shared asynchronous CPU ownership where it solves a real lifetime problem.

### Phase 2 checkpoint status — 2026-09-03

Implemented in this checkpoint:

- Added a standalone `Swim::Core` target with `EngineConfig`, `GraphicsBackend`, and `PhysicsBackend` contracts that do not depend on the legacy renderer implementation. `Auto`, Vulkan, legacy OpenGL, D3D12, Metal, PhysX, and Jolt are represented as runtime data even when a backend implementation is not available yet.
- Added runtime launcher parsing for `--graphics=...` and `--physics=...`, preserved `--state` and the Win32 editor-host compatibility argument, and made unsupported configured backends fail at startup with an explicit diagnostic instead of silently compiling a different backend.
- Removed the compile-time `SwimEngine::CONTEXT` renderer selector. The legacy Vulkan/OpenGL creation, resize, texture, cubemap, scene renderer binding, camera projection compatibility, and screen-space depth paths now follow the selected runtime backend.
- Removed core-system construction and frame iteration from `SystemManager`. `SwimEngine` now owns typed Input, Command, Scene, Physics, Renderer, and Camera slots and spells out Awake, Init, Update, FixedUpdate, and reverse-order Exit directly. The old dynamic `SystemManager` initially remained unreferenced; the 2026-09-05 retirement checkpoint archives it outside the active tree (see §0.3).
- Removed the process-global `SwimEngine` compatibility locator entirely. `main` owns one engine directly, core services and transitional renderer caches use unique engine ownership, scenes receive non-owning injected services, and first-party runtime code no longer calls `SwimEngine::GetInstance()` or the old Mesh/Texture/Material/Font/EntityFactory singleton locators.
- Added `SwimEngineConfigTests` covering defaults, split/equal-sign launcher syntax, backend resolution, state parsing, ShaderToy opt-in, and invalid backend diagnostics. Build-layout verification now rejects reintroducing compile-time renderer selection or stringly typed core ownership.
- Converted the typed core owners from transitional `shared_ptr` storage to `unique_ptr`: Input, Commands, Scenes, Physics, Camera, and the selected legacy renderer now have one explicit owner in `SwimEngine`. Public compatibility getters expose non-owning pointers rather than ownership-bearing smart pointers.
- Replaced Scene/Behavior/Physics/renderer service ownership with explicitly injected non-owning views. `SceneSystemServices` supplies the scene runtime dependencies, `PhysicsSystem` receives Scene/state views directly, and Vulkan/OpenGL receive camera plus scene-service dependencies directly. Runtime-registered scenes receive the same service injection path as preregistered scenes.
- Removed `SwimEngine::GetInstance()` from `CameraSystem`, Scene runtime-state/window-size/hotkey paths, `VulkanIndexDraw`, and renderer active-scene discovery. Camera projection convention and aspect now come from injected backend/surface data; Scene state comes from the engine-owned state reference; scene hotkeys dispatch through the injected command service; Vulkan indexed drawing receives renderer/scene/camera views from its owner instead of rediscovering the engine.
- Corrected lifecycle dependency order so low-level services initialize before Scene consumers, and shutdown destroys/resets Scene consumers before renderer/camera/physics dependencies. `SceneSystem::Exit()` now explicitly releases active/registered scenes while those dependencies are still alive. This is required now that those relationships are deliberately non-owning.

Validation completed in the checkpoint environment:

- `scripts/verify-build-layout.py` passes with the Phase 2 architecture invariants, including unique core ownership plus guards against CameraSystem, `VulkanIndexDraw`, or renderer scene access returning to global engine discovery.
- `EngineConfig.cpp` and `EngineConfigTests.cpp` compile and run directly with GCC/C++20.
- Offline CMake configuration succeeds with the legacy Windows engine disabled, and the `SwimCore`, `SwimEngineConfigTests`, and `SwimPlatformPublicHeaders` targets build successfully.
- The full legacy Windows/MSVC build now completes successfully after the Phase 2 ownership changes and Phase 3 scheduler integration; normal iteration remains on the established soft Windows build path.

The Phase 2 runtime-ownership migration is complete for the existing engine. First-party runtime code has zero `SwimEngine::GetInstance()`, Mesh/Texture/Material/Font pool `GetInstance()`, or `EntityFactory::GetInstance()` calls. The transitional renderer caches are engine-owned until Phase 4 replaces them with the authoritative asset model, and scene preregistration stores constructor metadata rather than process-global mutable Scene instances so multiple engine instances can construct independent runtime scenes. `BehaviorFactory::GetInstance()` remains only as behavior-type registration metadata and is intentionally deferred to the Phase 5 scene/plugin registration redesign; it is not used as a runtime service owner.

**Windows compile-fix checkpoint (2026-09-03):** A from-scratch Windows build now reaches normal C++ compilation with Ninja automatic regeneration suppressed, so soft builds are the default iteration path after the dependency cache exists. The first MSVC errors exposed two incomplete ownership migrations: `Transform` and `CubeMapController` still rediscovered `SwimEngine`. Transform hierarchy invalidation now uses a scene-wired non-owning registry context instead of the active-scene singleton, screen-space depth receives a generic `ClipSpaceDepthRange` selected once during engine startup, and cubemap controllers receive an already-created backend implementation from the owning renderer. Transform hooks are bound before user `Scene::Awake()` so transforms created during Awake receive their owning registry context. The verifier rejects reintroducing global engine discovery into these two paths or graphics-API branches into `Transform`.

### Phase 2 exit criteria

- [x] no first-party runtime subsystem uses `SwimEngine::GetInstance()` or the legacy pool/entity service locators.
- [x] engine runtime ownership no longer depends on process-global mutable engine/scene instances. *(Static scene/behavior type-registration metadata remains until Phase 5, but runtime Scene instances and services are per engine.)*
- [x] graphics and physics backend are selected through configuration/launcher args. *(Backends without implementations fail explicitly rather than falling back silently.)*
- [x] startup/shutdown dependency order is explicit.
- [x] core systems are not looked up by magic string names.

---

## Phase 3 — General jobs, async IO, and transient memory

Do this before expanding asset loading, render extraction, animation, or streaming.

### Job system

Use enkiTS as the recommended scheduler implementation behind `Swim::Jobs`.

Expose engine concepts such as:

- [x] `JobSystem`
- [x] `TaskGroup`
- [x] `JobHandle`
- [x] `ParallelFor`
- [x] task dependencies
- [x] priority
- [x] pinned/main-thread work where required
- [x] external thread registration if required
- [x] clean shutdown/cancellation policy

Do not expose enkiTS task classes throughout gameplay code.

### Retire the renderer-only thread-pool singleton

The useful parallel-for patterns in `ParallelUtils` can be adapted to the general scheduler.

The renderer should request jobs from `JobSystem`; it should not own a separate global CPU worker pool.

### Phase 3 scheduler checkpoint — 2026-09-03

Implemented at this safe compile boundary:

- Added pinned enkiTS v1.12 behind the standalone `Swim::Jobs` target; `TaskScheduler.h` is private to `JobSystem.cpp` and no enkiTS types leak into renderer/gameplay headers.
- Added engine-facing `JobSystem`, `JobHandle`, `TaskGroup`, priorities, dependencies, synchronous `ParallelFor`, main-thread pinned work, dedicated blocking lanes, external-thread registration, cooperative cancellation, and deterministic drain/shutdown behavior.
- `SwimEngine` uniquely owns the scheduler, injects it into renderer/scene runtime services, pumps main-thread pinned work around the frame update, waits outstanding engine work before consumer teardown, and shuts Jobs down after scene/render/input consumers are gone.
- Retired the renderer-only CPU worker singleton. `ParallelUtils`, `SceneBVH`, and Vulkan indexed-draw CPU work now route through the injected general scheduler.
- The legacy engine source glob explicitly excludes `Source/Engine/Jobs` because `JobSystem.cpp` is compiled once in `SwimJobs`; verification rejects accidentally compiling it into both targets.
- Added offline scheduler tests for dependency ordering, renderer-style parallel ranges, task groups, priorities, main-thread pinned work, cancellation, blocking-lane work, external-thread registration, and shutdown.
- The first full Windows dependency refresh successfully pulled enkiTS v1.12 and reached the legacy engine compile. MSVC then exposed a PCH-visible incomplete-owner issue in `Scene`: because `Scene` owns `std::unique_ptr<EntityFactory>` while `EntityFactory` is forward-declared there, both `Scene` constructors and its destructor now remain out-of-line in `Scene.cpp`, where `EntityFactory` is complete.
- The same Windows compile-readiness pass found and fixed a latent `FontPool::Flush()` declaration/definition mismatch before the next MSVC pass could reach it. Verification now guards both boundaries.
- The next real Windows soft build passed the PCH boundary and compiled into the renderer before stopping in `OpenGLRenderer.cpp`: shutdown called `MaterialPool::Flush()` through a forward declaration without including `MaterialPool.h`. Renderer/scene runtime-service call sites now include the concrete service headers they dereference directly, and the verifier guards the affected boundaries so these MSVC-only incomplete-type regressions do not return.
- The following Windows soft build reached 59/66 first-party build steps before `CubeMapControlTest.cpp` exposed another PCH-masked dependency boundary: the demo referenced `CubeMapController`/`CubeMap` and dereferenced `Renderer`, `Scene`, and `InputManager` without concrete declarations/includes. The demo now uses a non-owning `CubeMapController*` helper instead of leaking the renderer-owned `unique_ptr`, checks the controller before all use, and includes every concrete service it dereferences. The same direct-service include rule is enforced across `Source/Game` so this class of forward-declaration compile failure is caught before MSVC.
- The same compile-readiness sweep removed the remaining project-local include case mismatches (`SetTextCallBack.h`, `RigidBody.h`, and the two lowercase `pch.h` uses). Windows had hidden these because its filesystem is case-insensitive; keeping the spelling aligned with the actual files avoids carrying avoidable failures into the later Linux legacy-engine migration.
- The subsequent Windows soft-build cycle cleared the remaining PCH-masked concrete-type/include issues and now completes the full legacy `SwimEngine` build and final link successfully. This is the commit/checkpoint boundary for the Phase 2 ownership migration plus the Phase 3 scheduler foundation.

The scheduler/ownership checkpoint was Windows/MSVC validated through a successful full legacy engine link. **Historical sequencing note:** the remaining work at that checkpoint was Async IO, transient memory, and Phase 3 exit validation; all three are now complete in the later checkpoints below, and Phase 4 is active.

### Async IO service

Create a platform-neutral IO layer:

- [x] async full-file read;
- [x] async range read;
- [x] memory mapping;
- [x] priorities;
- [x] cancellation;
- [x] batch/adjacent-read opportunities;
- [x] completion on a known executor/thread;
- [x] explicit blocking API for tools/bootstrap/tests only.

Do not hide blocking filesystem work behind an apparently cheap asset getter.

### Phase 3 Async IO checkpoint — 2026-09-03

Implemented in this checkpoint:

- Added the standalone `Swim::IO` module with engine-facing `AsyncIoService`, `ReadRequest`, full-file reads, exact range reads, multi-range reads, priorities, cooperative cancellation, explicit status/error reporting, and explicit blocking entrypoints for bootstrap/tools/tests. The implementation is platform-neutral C++ and uses `Swim::Jobs::ScheduleBlocking()` rather than creating IO-owned threads or `std::async` workers.
- Async reads execute on the scheduler's reserved blocking lanes so filesystem stalls cannot consume normal compute-worker capacity. The service requires a running `JobSystem` with at least one blocking lane and rejects new work after shutdown begins.
- Completion callbacks are never executed on an IO lane. Completed requests are queued internally and dispatched only by `AsyncIoService::PumpCompletions()` on the thread which initialized the service. `SwimEngine` initializes IO on its main thread and pumps completions before and after the normal frame update, giving future asset/streaming code a documented completion executor rather than an arbitrary worker callback.
- Added adjacent/overlapping batch-range coalescing. `ReadRangesAsync()` preserves caller range order while sorting internally, merging overlapping/adjacent reads (plus an optional small `MaxCoalesceGapBytes`), performing fewer contiguous file reads, and scattering the requested chunks back into stable result slots. This establishes the package-streaming primitive needed by `.sasset/.spack` work without baking asset semantics into IO.
- Memory mapping remains owned by `Swim::Platform::MappedFile`; `AsyncIoService::MapFileReadOnlyBlocking()` exposes it through the IO boundary while keeping the operation explicitly blocking. Full-file/range blocking helpers are named as blocking APIs so runtime asset getters cannot accidentally disguise synchronous disk work.
- Cancellation is cooperative and deterministic: queued work observes cancellation before touching the filesystem; an already-running portable blocking read is allowed to finish, its data is discarded if cancellation was requested, and the request reaches `Cancelled` before its main-thread completion is dispatched. Shutdown supports drain or cancel-pending behavior.
- `SwimEngine` uniquely owns `AsyncIoService`, injects it into transitional renderer/scene service views, exposes it to scenes without global discovery, drains IO callbacks while those consumers are still alive, and only then tears consumers down and shuts Jobs down. This preserves the Phase 2 lifetime rules.
- Added `SwimAsyncIoTests` coverage for full reads, exact ranges, coalesced batch ranges, concurrent reads, failed IO, cancellation, owner-thread completion dispatch, memory mapping, and IO-before-Jobs shutdown. Added a public-header compile target and verifier guards for the module boundary, legacy source-glob exclusion, engine ownership/injection, main-thread pumping, and shutdown order.

Validation completed in the checkpoint environment:

- `scripts/verify-build-layout.py` passes with the Async IO architecture checks.
- Offline CMake configure succeeds with the legacy engine disabled, and the standalone IO public header plus fallback `Swim::Jobs` target compile under GCC/C++20.
- `AsyncIoService.cpp` compiles independently under GCC/C++20, and a temporary-file runtime smoke test passed full-file reads, batched/coalesced ranges, failed-read reporting, mapping, completion dispatch, and deterministic shutdown using the scheduler fallback.
- The next real Windows clean build reached the final legacy link, which proves the Async IO translation units and their engine integration compiled under MSVC. The link failure was instead caused by the top-level legacy source filter accidentally excluding `Source/Engine/Systems/IO/CommandSystem.cpp` and `InputManager.cpp`; that filter is corrected at the following memory/build checkpoint.

**Historical sequencing note:** transient memory was the next task at this Async IO checkpoint; it is completed in the following checkpoint and Phase 3 has since closed.

### Memory strategy

Add simple, purposeful allocators before hot paths proliferate:

- [x] per-frame CPU arena;
- [x] per-job scratch arena or thread scratch;
- [x] temporary import/compiler arena primitive is available through the same chunked `LinearArena`/`ScratchScope` foundation; importer/compiler adoption happens when those Phase 4/asset-compiler paths exist;
- [x] frame/scratch allocations are explicitly transient and may not back persistent registries. Persistent asset registries must use stable containers/slot maps/handles rather than retaining arena pointers.

Use mimalloc as the engine's general-purpose heap and backing allocator for these arenas, but keep lifetime-oriented arenas explicit. Do not build a giant custom general allocator unless profiling demonstrates a need beyond mimalloc plus focused frame/scratch allocation.

### Phase 3 transient memory + mimalloc checkpoint — 2026-09-03

Implemented in this checkpoint:

- Added pinned Microsoft mimalloc v3.4.5 through `cmake/MemoryDependencies.cmake`. Swim builds the static library and upstream override object only; shared-library/redirection-DLL and dependency tests are disabled. Architecture-specific `MI_OPT_ARCH` is left off so the allocator does not silently raise the engine's baseline CPU requirement.
- The final legacy `SwimEngine` executable consumes `mimalloc-obj`, which is the upstream static-override path and matches Swim's existing static `/MT` MSVC CRT contract. This makes ordinary process `malloc/free` and C++ allocation resolve through mimalloc without spreading allocator-specific calls throughout gameplay/renderer code. `Swim::Memory` additionally links the normal static target for explicit arena backing.
- Added the standalone `Swim::Memory` module. `LinearArena` is a chunked bump allocator whose growth adds blocks instead of reallocating an existing block, so earlier allocations are not invalidated. Blocks are backed by `mi_malloc_aligned`/`mi_free` in real builds, retained across reset for reuse, and expose used/reserved/peak/block statistics. Markers support nested scoped rewinds, carry a reset generation so stale markers fail instead of silently rewinding a later frame, and reject fabricated forward rewinds. Allocation-size/alignment arithmetic is overflow-checked before growing a block.
- Added engine-owned `FrameArena`. `SwimEngine` resets it once at the beginning of each accepted simulation/render frame, before fixed/update work, and injects a non-owning view into renderer runtime services and Scene services. Persistent objects must never retain pointers into this arena across `BeginFrame()`.
- Added thread-local scratch storage plus RAII `ScratchScope`. Every JobSystem callback path (normal jobs, `ParallelFor` partitions, main-thread pinned work, blocking-lane work, and the fallback implementation) establishes a scratch scope automatically. Nested scopes rewind to their entry marker when they exit, so per-job temporary allocations are reclaimed without cross-thread synchronization while the thread retains its blocks for reuse.
- Added `SwimMemoryTests` covering alignment, chunk growth, marker rewind, retained capacity after reset, per-frame reset/indexing, and thread scratch scope rewind.
- Fixed the real Windows linker failure discovered immediately before this checkpoint. Legacy source exclusions are now anchored to exact `Source/Engine/<module>/` roots; the old broad `/IO/` rule had removed `Systems/IO/CommandSystem.cpp` and `Systems/IO/InputManager.cpp` from the executable. The verifier now rejects broad module-name exclusions so nested legacy directories cannot be silently dropped again.

Lifetime rules established by this checkpoint:

- frame memory is valid only until the next `FrameArena::BeginFrame()`;
- scratch memory is valid only until the owning `ScratchScope` exits;
- arena allocation does not run object destructors automatically, so use it for trivially destructible temporary data or explicitly destroy non-trivial objects before rewind/reset;
- pointers/references from frame/scratch memory must not be stored in persistent ECS components, asset registries, renderer residency structures, async completions, or jobs that outlive the allocation scope;
- mimalloc remains the general heap for persistent/irregular allocations; frame/scratch arenas exist to encode lifetime and reduce hot-path allocation churn, not to replace every container allocator.

Validation completed in the checkpoint environment:

- `scripts/verify-build-layout.py` passes, including the mimalloc pin/target contract, exact legacy source exclusions, arena ownership/injection, automatic per-job scratch scopes, and the guard against reintroducing broad `/IO/`/`/Jobs/` exclusions.
- Offline CMake configuration succeeds with the legacy Windows engine disabled.
- `SwimMemoryTests`, `SwimJobSystemTests`, and `SwimEngineConfigTests` compile and run successfully under GCC/C++20 using the dependency-free fallback allocator path.
- A fully dependency-free rebuild of `SwimAsyncIoTests` is not possible because the existing offline SDL stub does not provide SDL headers needed by `SwimPlatform`; this is an existing offline-stub limitation, not a memory/mimalloc regression. The earlier real Windows clean build already compiled the Async IO integration through to the final executable link.
- **Windows/MSVC validation completed:** the required clean build fetched/configured mimalloc v3.4.5, compiled the static override under the existing `/MT` contract, preserved the corrected exact module source filters, and completed the final `Swim Engine.exe` link.

### Phase 3 exit criteria

- [x] renderer code can use a general `ParallelFor` without knowing enkiTS.
- [x] asset loader has a non-blocking read primitive available.
- [x] no new thread-per-file or thread-per-subsystem patterns; compute and reserved blocking work share the engine-owned scheduler.
- [x] jobs and IO shut down deterministically.
- [x] engine-owned per-frame transient memory has a documented one-frame lifetime and no global owner.
- [x] JobSystem callbacks automatically receive thread-local scratch lifetime without exposing scheduler internals.
- [x] general persistent heap allocation is routed through the pinned mimalloc integration in real builds, with explicit arena backing also using mimalloc.
- [x] final Windows/MSVC clean build fetches mimalloc v3.4.5 and links the legacy executable successfully with the corrected source filters and static allocator override.

**Phase 3 is complete.** The real Windows/MSVC clean-build gate above has been validated; Phase 4 asset identity/residency is now the active implementation phase.

---

## Phase 4 — Asset identity, source import, compiled formats, and ownership

This phase intentionally precedes the new renderer's real asset residency path. The renderer should be written against final asset identities, not the current singleton `shared_ptr` pools.

### Split the asset problem into four layers

```text
Source Asset
(glTF/GLB/PNG/JPEG/WebP/KTX2/font/audio/etc.)
       |
       v
Importer / Compiler
       |
       v
Compiled Runtime Asset
(.sasset / .spack)
       |
       v
CPU Runtime Residency
       |
       v
GPU/Audio/Physics Residency
```

These are separate stages with separate owners.

### Asset identity

Introduce:

```cpp
struct AssetId
{
    uint64_t Value;
};

template<typename T>
class AssetHandle;
```

Requirements:

- [x] stable identity independent of a raw pointer;
- [x] typed handles;
- [x] stale-handle detection/generation where useful;
- [x] path-to-AssetId database for authoring/dev;
- [x] content hash for incremental compilation and deduplication;
- [x] dependency graph;
- [x] explicit load state;
- [x] explicit errors;
- [x] handles can exist before residency completes.

### Phase 4 asset identity + CPU schema checkpoint — 2026-09-03

The first Phase 4 dependency boundary is now implemented as the standalone `Swim::Assets` module and is the required identity/ownership surface for all new renderer/asset work:

- `AssetId` is a stable 64-bit logical identity and `AssetHandle<T>` adds typed generation tracking. `Forget()` advances the generation so stale handles cannot silently resolve to a replacement residency.
- `AssetDatabase` canonicalizes logical relative asset paths, provides deterministic path-to-`AssetId` creation, supports explicit `Bind`/`Rebind` so authoring renames can preserve identity, and can snapshot mappings for the later persistent authoring database/compiler layer.
- `ContentHash` is a portable SHA-256 value with known-vector tests. Runtime/compiler code can index identical payloads by content rather than performing O(N) raw-byte scans; multiple logical asset identities may intentionally share the same content hash.
- `AssetSystem` is engine-owned, owner-thread mutated, and has explicit `Unloaded -> Queued -> Loading -> Resident/Failed` state, structured error codes/messages, forward/reverse dependency metadata, self/cycle rejection, content-hash lookup, unload vs. identity-forget semantics, and handles that are valid before residency completes.
- Residency ownership is centralized in `AssetSystem` through unique ownership/type erasure rather than returning owning `shared_ptr`s from another global pool. Scenes and transitional renderer services receive a non-owning `AssetSystem` service pointer; no new code needs global discovery.
- Backend-neutral CPU schemas now exist for `MeshAsset`, `TextureAsset`, `SamplerAsset`, `MaterialTemplateAsset`, `MaterialInstanceAsset`, and `ModelAsset`. They contain CPU metadata/payload bytes and typed asset references only: no Vulkan/OpenGL handles, renderer pointers, GPU heap offsets, importer object graphs, EnTT state, or `shared_ptr` ownership. `ModelAsset` binds a mesh identity and independent material-instance identities per node/slot; materials do not own meshes.
- `SwimAssets` is an explicit first-party CMake target, excluded from the legacy source glob and linked once into `SwimEngine`. `SwimAssetPublicHeaders`, `SwimAssetSystemTests`, and `SwimHeadlessCoreAssets` exercise the same public target boundary real applications will use.
- `scripts/verify-build-layout.py` now guards the exact Assets source exclusion, module/test target contracts, engine lifecycle/injection, required identity/hash/dependency APIs, backend-neutral CPU schema shape, and rejects Vulkan/OpenGL/importer/EnTT/`shared_ptr` leakage into `Source/Engine/Assets`.

Validation completed for this checkpoint:

- standalone GCC/C++20 asset tests pass, including SHA-256 known vectors, path normalization/rebind, pre-residency handles, load/failure transitions, typed resolution, stale generations, duplicate content hashes, reverse dependencies, self/cycle rejection, and independent model mesh/material identities;
- a fresh offline CMake configure/build succeeds for `SwimAssetSystemTests`, `SwimAssetPublicHeaders`, `SwimHeadlessCoreAssets`, `SwimJobSystemTests`, and `SwimMemoryTests`;
- all of those executable tests pass and `scripts/verify-build-layout.py` passes;
- the engine integration changed first-party C++ only and adds no new dependency pin, so the next real Windows validation is a **soft build**, not a clean dependency rebuild.

**Next Phase 4 work:** migrate the legacy mesh/texture/material ownership surfaces toward these CPU asset types and handles. The schemas are ready, but the old `Mesh`, `Texture2D`, `MaterialData`, `MeshPool`, `TexturePool`, and `MaterialPool` consumers still exist; therefore the broader replacement/Phase 4 exit criteria remain open.

### Phase 4 renderer ownership migration checkpoint — 2026-09-03

Critical-path items 13 and 14 are now complete at the ownership/data-boundary level. The legacy renderer still exists, but it no longer forces the new runtime asset model to inherit its old geometry/material coupling:

- the transitional legacy `Mesh` is now CPU geometry only (`vertices` + `indices`); backend buffer offsets/handles and the generated local AABB remain in the separate renderer-owned `MeshBufferData` residency record managed by `MeshPool`;
- `MaterialData` no longer stores or owns a mesh. `LegacyRenderBinding` is a clearly named compatibility draw record that pairs an independent CPU mesh, renderer mesh residency, and material only at the draw boundary while the old renderer is being retired;
- scene `Material`/`CompositeMaterial`, OpenGL draw code, Vulkan draw extraction/instance code, scene BVH/debug draw/gizmos, and serialization compatibility paths now consume that binding rather than treating geometry as material state;
- scene BVH local bounds are derived directly from CPU mesh vertices instead of reaching through a mesh object into backend residency;
- `MeshPool` keeps CPU mesh identity and renderer residency in separate maps and exposes an explicit `GetMeshBufferData()` compatibility lookup; removing/flushing a mesh now tears down the associated ID/residency bookkeeping instead of leaving stale entries;
- the legacy mesh safe-registration path and tinygltf embedded-image texture path now use SHA-256 `ContentHash` indices for deduplication. The prior O(N) raw-byte pool comparisons were removed, including the disabled debug texture comparison;
- `MaterialData.h` is geometry-free. Backend/geometry includes needed only for the compatibility draw record live in `LegacyRenderBinding.h`, making accidental ownership regression easier to detect;
- `scripts/verify-build-layout.py` now rejects geometry/backend state returning to `MaterialData` or CPU `Mesh`, requires the separate renderer-residency seam, requires hash-indexed mesh/texture dedup, and rejects a return to raw-byte `memcmp` scans in those pools.

This checkpoint does **not** claim that the legacy pools are the final asset system. `Texture2D`, `MeshPool`, `TexturePool`, `MaterialPool`, tinygltf runtime loading, and backend-specific residency still exist as compatibility code and remain scheduled for removal after the compiler/runtime package path is ready.

Validation completed for this checkpoint:

- `scripts/verify-build-layout.py` passes with the new ownership/regression guards;
- the platform-neutral `Swim::Assets` public/runtime tests remain the source-of-truth validation for independent typed mesh/material/texture identities;
- no dependency pin changed in this checkpoint, so Windows/MSVC should validate it with the normal **soft build**.

**Next Phase 4 work:** critical-path item 15 — introduce a fastgltf-only source importer/tool boundary and a Swim-owned intermediate model representation. fastgltf types must terminate inside that importer; runtime assets and runtime model loading must not retain importer object graphs.

### Phase 4 fastgltf importer checkpoint — 2026-09-03

Critical-path item 15 is now complete at the source-import boundary:

- `SwimAssetCompiler` is a tool-side target with `fastgltf::fastgltf` as a private dependency; `Swim::Assets`, renderer/runtime targets, public importer headers, and `IntermediateModel.h` do not expose fastgltf types;
- `GltfImporter` owns the entire fastgltf object graph and translates it immediately into a Swim-owned `IntermediateModel` containing source nodes/hierarchy, decomposed transforms, meshes/primitives, material-slot indices, metallic-roughness material data, textures/samplers, source images, and root-node identity;
- the importer handles indexed and generated-index primitives, optional normals/tangents/UV0, external buffers/images, embedded/data-source image bytes, GLB/buffer-view image payloads, and structured import failures;
- deliberately supported source extensions are currently `KHR_mesh_quantization`, `KHR_texture_basisu`, `EXT_texture_webp`, `MSFT_texture_dds`, and `KHR_materials_unlit`. `KHR_texture_transform` is intentionally not advertised until its transform semantics are preserved by the intermediate representation;
- skins/skeletons, animation channels, morph targets, and camera/light import remain explicit future importer expansion and are not silently discarded under a claimed-support flag; *(2026-09-24: skins, animation channels and morph targets are now imported and compiled, item 78; cameras and lights remain)*
- simdjson v3.12.3 is pinned and provided before fastgltf v0.9.0. This prevents fastgltf's v0.9.0 dependency fallback from downloading a simdjson single-header file into its own CPM source checkout, preserving the repository rule that cached dependency sources are immutable;
- `scripts/verify-build-layout.py` now enforces the importer boundary, dependency pins/order, private fastgltf linkage, absence of fastgltf/tinygltf types from the intermediate/public/runtime asset boundary, and the immutable-cache audit;
- `SwimAssetCompilerPublicHeaders` compiles in the dependency-free/offline configuration, while `SwimGltfImporterTests` provides a real tiny glTF import smoke test for dependency-enabled builds.

Validation completed for this checkpoint:

- the repository verifier passes;
- fresh offline CMake configure/build passes for the AssetCompiler public headers and the existing Assets/Core/Jobs/Memory foundation tests;
- the existing foundation executables continue to pass;
- the implementation/API contract was checked against the exact fastgltf v0.9.0 interfaces, but this environment cannot fetch the external source checkout for a dependency-enabled compile. Because fastgltf v0.9.0 and simdjson v3.12.3 are new pins, the next real Windows/MSVC validation must be a **clean build**.

**Next Phase 4 work:** critical-path item 16 — run meshoptimizer as an offline compiler pass over `IntermediateModel` primitives, beginning with vertex-cache/fetch/overdraw optimization while keeping meshoptimizer out of runtime/public asset APIs.

### Phase 4 meshoptimizer checkpoint — 2026-09-03

Critical-path item 16 is now complete for the foundational offline geometry optimization pass:

- meshoptimizer v1.1 is pinned only in `AssetCompilerDependencies.cmake`, with demo/gltfpack/shared/install paths disabled; the `meshoptimizer` target is linked privately by `SwimAssetCompiler`;
- `MeshOptimizer.h` exposes only Swim-owned options/stats/errors and `IntermediateModel`; the third-party header is included only by `MeshOptimizer.cpp`;
- triangle-list primitives run vertex-cache optimization first, overdraw optimization second, and vertex-fetch optimization/compaction last, matching meshoptimizer's ordering requirements;
- non-triangle primitives remain untouched for now rather than being incorrectly treated as triangle lists;
- the pass validates triangle index counts and index bounds before mutating the model, returns structured compiler errors, removes unused vertices during fetch compaction, and recalculates bounds after vertex reordering/compaction;
- `SwimMeshOptimizerTests` covers compaction, unchanged non-triangle data, bounds regeneration, invalid indices, and invalid overdraw configuration;
- `scripts/verify-build-layout.py` now enforces the v1.1 compiler-only dependency pin, public-header isolation, private compiler linkage, pass presence, and cache -> overdraw -> fetch ordering.

Validation completed for this checkpoint:

- repository verification passes;
- the fresh offline foundation build still passes, including `SwimAssetCompilerPublicHeaders`;
- the first-party optimizer implementation and test compile/run cleanly under GCC/C++20 against a local contract shim matching the exact meshoptimizer v1.1 function signatures used by Swim;
- the real meshoptimizer source cannot be fetched in this execution environment, so the dependency-enabled compiler/test build remains part of the same required **clean Windows build** introduced by item 15.

LOD simplification and meshlet generation remain separately unchecked processing stages below; integrating the meshoptimizer library does not imply those products already exist.

**Next Phase 4 work:** critical-path item 17 — define the KTX2 compiled texture/runtime metadata boundary and move source-image decode/transcode concerns into the compiler side without allowing KTX/source decoder object graphs into runtime asset ownership.

### Phase 4 KTX2 runtime/compiler metadata checkpoint — 2026-09-03

Critical-path item 17 is implemented. KTX2 is now a Swim-owned runtime metadata/container boundary rather than a renderer/backend object:

- `Ktx2Container` validates the KTX2 identifier, dimensions, face count, level index, level byte ranges, and uncompressed-size rules without including Vulkan/KTX-Software types;
- runtime metadata preserves 1D/2D/3D/cube shape, array layers, mip offsets/sizes/uncompressed sizes, the original container format code, and BasisLZ/Zstandard/Zlib supercompression identity;
- common Vulkan-format numeric codes carried by KTX2 are translated immediately into backend-neutral `TexturePayloadFormat` values, including RGBA8, RGBA16F, BC1/3/5/7, ETC2 RGBA8, and ASTC 4x4 linear/sRGB variants;
- typed KTX2 formats are authoritative for linear/sRGB interpretation. Universal/undefined-format KTX2 payloads retain compiler policy until the later DFD/BasisU transcoding work exists;
- `Ktx2TextureCompiler` validates a KTX2 container and emits a backend-neutral `TextureAsset` payload. Runtime assets retain KTX2 bytes/streamable level metadata, not libktx/importer object graphs;
- tests cover malformed/truncated level data, compressed-level metadata, common backend-neutral format mapping, multi-level mip metadata, cube arrays, and compiler propagation into `TextureAsset`;
- source image decode/encode into KTX2 is intentionally still open. This checkpoint consumes already-KTX2 source payloads; PNG/JPEG/WebP -> KTX2/native compression and BasisU transcoding remain compiler-side follow-up work rather than being hidden inside runtime texture construction.

### Phase 4 `.sasset` v1 + development auto-cook checkpoint — 2026-09-03

Critical-path item 18 is implemented around a versioned, chunked runtime container and a single shared development cook path:

- `.sasset` v1 has fixed magic/schema/header sizing, asset type, stable `AssetId`, payload/content hash, compiler-profile hash, source-graph hash, dependency table, chunk table, per-chunk hash/compression/alignment/size metadata, canonical logical path, and optional source provenance;
- persisted references store `AssetId`, never runtime generations or backend resource handles. `LoadSasset()` reconstructs typed handles through the engine-owned `AssetSystem` and publishes decoded CPU assets only after validation;
- the first static-model compiler converts a Swim `IntermediateModel` into separate mesh/material-template/material-instance/texture/sampler/model `.sasset` objects. The root model and its dependencies keep independent identities and the mesh payload is bulk-copy-friendly CPU data;
- a deterministic compiler-profile fingerprint includes the `.sasset` schema plus fastgltf/meshoptimizer/runtime-packing policy, making a compiler-policy change invalidate old cooked roots;
- loose development `.gltf`/`.glb` scanning is owned by `DevelopmentAssetPipeline`, not `Swim::Assets`. Its order is source discovery -> fastgltf import -> meshoptimizer -> static-model compilation -> cooked publication -> normal `.sasset` runtime loading;
- source provenance records the source model plus local external URI dependencies such as `.bin` and image files. Startup hashes that graph, so changing an external buffer invalidates the model even when the `.gltf` JSON did not change;
- a cooked root is current only when its compiler profile/source graph match and every recursively referenced cooked object still exists, parses, and passes its hashes; missing/corrupt nested objects therefore trigger recooking;
- cooked dependencies are published before the root. File replacement uses `.new` plus rollback-capable `.old` staging, so failed replacement preserves the last good object and a new root is never advertised before its dependency files are in place;
- authoring layout is mirrored: `Assets/Models/Foo.glb` cooks to `Assets/Cooked/Models/Foo.sasset`, while dependency objects are stored by stable `AssetId` under `Assets/Cooked/.objects/`;
- with `SWIM_ENABLE_DEV_ASSET_AUTOCOOK=ON`, engine initialization runs this bootstrap against the platform filesystem's asset root and immediately loads current/newly-cooked roots into `AssetSystem`; shipping builds can disable the compiler/bootstrap while retaining the same `.sasset` runtime reader;
- `SwimAssetCooker [asset-root]` invokes the exact same bootstrap without launching the renderer, so CI/manual pre-cooking and engine-start auto-cooking cannot drift into two different pipelines;
- `SwimSassetFormatTests` validates format/hash/load round trips and corruption rejection; `SwimStaticModelCompilerTests` compiles and reloads a static triangle model from a Swim intermediate model; `SwimDevelopmentAssetPipelineTests` is the dependency-enabled end-to-end fixture for missing-root cook, unchanged-source skip, missing nested-object repair, external `.bin` invalidation, recook, and hot replacement under stable identity.

Validation in this environment: a fresh offline CMake configure/build passes for `Swim::Core`, `Swim::Memory`, `Swim::Jobs`, `Swim::Assets`, public-header checks, `SwimAssetSystemTests`, `SwimKtx2ContainerTests`, and `SwimHeadlessCoreAssets`; standalone `.sasset`, static-model, and KTX2 compiler tests also pass, including AddressSanitizer/UndefinedBehaviorSanitizer runs for the serialization/KTX2/static-model paths. Repository verification passes under GCC/C++20. The container cannot fetch the pinned fastgltf/meshoptimizer/simdjson source trees, so the dependency-enabled importer/bootstrap fixture and the complete legacy Windows executable remain the required MSVC validation. Because those compiler dependencies are new relative to the last Windows-validated repository snapshot, the **first Windows validation for this checkpoint must be a clean build** so the pinned sources are fetched; ordinary builds can return to the soft path after that cache exists. The uploaded repository contains no loose project `Assets` tree, so there was no real project GLTF/GLB to pre-cook during this execution.

### Phase 4 item-18 commit-safe checkpoint — 2026-09-03 *(historical boundary)*

At this checkpoint, the repository was intentionally frozen at the item-18 boundary before the larger renderer-residency migration began:

- all first-party game/test call sites have been moved off the removed geometry-coupled `RegisterMaterialData` / `GetMaterialData` API and now use `LegacyRenderBinding`; the verifier rejects regressions to those stale calls;
- `MaterialPool` still owned the old tinygltf source-import compatibility path. It had **not** been half-converted to `AssetSystem`; that removal was reserved for item 19 as one coherent adapter/residency migration;
- engine startup development auto-cooking loaded authoritative cooked CPU assets into `AssetSystem`, while existing renderer/game consumers continued using their explicitly marked legacy bindings until item 19 connected those two sides;
- no Phase 4 checkbox beyond item 18 is claimed by this compatibility cleanup.

This was the intended commit boundary: source import/cooking and runtime `.sasset` loading were established while the legacy renderer still had one coherent compatibility path. The cooked-residency checkpoint below supersedes that temporary boundary.

**Windows clean-build compile follow-up (2026-09-03):** the required clean MSVC run successfully fetched/configured the new mimalloc/simdjson/fastgltf/meshoptimizer dependency set, completed PhysX, and reached 705/706 build steps. The only compiler diagnostic in the full log was `Sandbox.cpp` passing a `std::shared_ptr<LegacyRenderBinding>` directly to `Scene::AddComponent<Material>` even though `Material` intentionally exposes an explicit compatibility-binding constructor. The stale call site (and its adjacent commented examples) now wraps the binding as `Engine::Material(binding)`, matching every other migrated scene/test call site. No dependency declaration changed in that fix, so the next Windows validation remained the normal soft build. The item-19 residency work below is the subsequent implementation step; it still requires that normal Windows soft-build validation for the Windows-only legacy renderer.

### Phase 4 cooked model -> legacy renderer residency checkpoint — 2026-09-03

The first critical-path item 19 residency cut is now implemented without reintroducing a second asset registry:

- `MaterialPool` is still a temporary renderer compatibility surface, but it now receives the engine-owned `AssetSystem` explicitly and resolves the authoritative cooked `ModelAsset`, `MeshAsset`, `MaterialInstanceAsset`, and `TextureAsset` handles instead of parsing source GLB data itself;
- source model paths used by existing scene code are reduced to authoring lookup keys (`Assets/Models/Foo.glb` -> logical `Models/Foo.model`). No tinygltf object graph, source buffer/image view, or Draco decoder survives in `MaterialPool`;
- model node hierarchy transforms are reconstructed from the cooked `ModelAsset`, then baked into the temporary legacy vertex payload so existing scene/entity transform behavior stays compatible while the renderer still uses `LegacyRenderBinding`;
- mesh primitives are rebuilt from the cooked interleaved vertex/index payload and handed to the existing content-hashed `MeshPool` residency seam. Material slots resolve independently through the model's material handles rather than restoring mesh/material ownership coupling;
- `TexturePool::GetOrCreateTextureFromAsset()` adds a temporary cooked-texture residency adapter keyed by typed asset identity, generation, and current asset content hash, with decoded-content dedup underneath it. It accepts raw RGBA8, Zstandard RGBA8, and KTX2/BasisU payloads and converts only the base level into the current `Texture2D` compatibility object;
- the runtime target no longer contains or links `TinyGltfImplementation.cpp`, `tinygltf`, Draco, or libwebp. Those packages were removed from the legacy dependency graph rather than left configured but unused; BasisU transcoding and zstd remain because the temporary renderer residency adapter explicitly consumes those cooked runtime payloads;
- at this historical residency checkpoint the active Sponza compatibility sample was moved to the non-Draco KTX2 source rather than restoring Draco to runtime. Compiler-side Draco support was added in the later source-codec ownership checkpoint, preserving this runtime boundary;
- `AssetSystem::ComputeDependencyRevisionHash()` fingerprints a model plus its declared dependency graph using stable ids, generations, load state, content hashes, and dependency edges. `MaterialPool` validates its compatibility cache against that revision, so a dev recook published under the same stable handle generation cannot return stale mesh/material/texture residency;
- `scripts/verify-build-layout.py` now rejects reintroducing the old `LoadAndRegisterCompositeMaterialFromGLB` API, runtime tinygltf/Draco/WebP links/packages, the tinygltf implementation TU, or source-import symbols inside `MaterialPool`, while requiring the cooked model/texture residency seam.

Validation for this cut:

- repository build-layout verification passes;
- a fresh offline CMake configure succeeds with the Windows-only legacy engine disabled;
- `SwimAssetSystemTests`, `SwimKtx2ContainerTests`, `SwimAssetPublicHeaders`, and `SwimAssetCompilerPublicHeaders` build successfully, and the two runnable asset tests pass; the asset-system test also verifies that republishing a dependency changes the graph revision without changing its stable handle generation;
- the legacy renderer itself remains Windows-only, so the next Windows validation is the normal **soft build** against the existing dependency cache. A clean dependency rebuild is no longer required by this change because no new third-party revision was introduced; in fact three obsolete runtime source-import dependencies were removed.

This does **not** close the broader Phase 4 renderer-residency migration. `MeshPool`, `TexturePool`, `MaterialPool`, `LegacyRenderBinding`, and `Texture2D` still exist as compatibility surfaces, and `Texture2D` still performs renderer upload work during construction, so the corresponding exit criterion remains open.

### Phase 4 ordinary source-image compiler checkpoint — 2026-09-03

The ordinary glTF source-image path now crosses the same compiler/runtime boundary as KTX2 instead of depending on renderer-side source decoders:

- `SourceImageTextureCompiler` owns PNG/JPEG/WebP authoring decode. PNG/JPEG use a compiler-private stb implementation and WebP uses pinned libwebp; neither decoder type leaks through a first-party compiler API;
- `StaticModelCompiler` detects source image MIME from declared metadata or file magic. KTX2 continues through `Ktx2TextureCompiler`, while PNG/JPEG/WebP are decoded into a backend-neutral cooked `TextureAsset`; unsupported DDS/unknown formats fail as unsupported source data instead of silently reaching runtime;
- ordinary source images currently emit one base-level `NativeMipData` RGBA8 payload, preserving requested texture semantic and sRGB/linear color-space metadata. This closes source-image runtime parity without pretending final compression/mip policy is complete;
- libwebp is now owned by `AssetCompilerDependencies.cmake` only. The runtime target does not link it. The shared pinned stb source is used privately by the compiler for PNG/JPEG and separately by the legacy runtime only for still-loose compatibility textures/fonts;
- the compiler-side stb implementation has TU-local linkage because the development compiler may be linked into the legacy executable. The runtime stb implementation is explicit in `StbImageImplementation.cpp`; it no longer arrives accidentally as a side effect of the removed tinygltf implementation TU;
- both runtime stb implementation TUs are explicitly excluded from the legacy PCH, preventing implementation macros from being instantiated through precompiled-header inclusion order;
- the static-model compiler profile hash is bumped to `texture=ktx2-or-rgba8-source-v2`, so previously cooked roots cannot be mistaken for outputs from this source-image policy;
- `SwimSourceImageTextureCompilerTests` covers PNG/WebP decode, JPEG/MIME recognition, cooked dimensions/format/color-space, and unsupported DDS behavior. The repository verifier requires the compiler-only WebP boundary and rejects libwebp use outside the source-image compiler implementation.

Validation for this cut:

- fresh offline CMake configure plus `SwimAssetSystemTests`, `SwimKtx2ContainerTests`, `SwimAssetPublicHeaders`, and `SwimAssetCompilerPublicHeaders` remain green;
- `SourceImageTextureCompiler.cpp` passes a standalone C++20 syntax compile against the platform libwebp headers;
- the real pinned dependency-enabled `SwimSourceImageTextureCompilerTests` target and the Windows-only legacy executable still require the next Windows soft build against the existing dependency cache. No dependency revision changed: stb and libwebp use the same pinned revisions already present in the repository's prior runtime dependency graph.

This checkpoint deliberately leaves **`convert textures to KTX2/native compressed formats` unchecked**. PNG/JPEG/WebP now become cooked assets before runtime, but they are currently uncompressed RGBA8 base-level payloads; mip generation plus final KTX2/native compressed distribution policy is still future Phase 4 work. It also does not close **`no asset constructor uploads to renderer`** because the compatibility `Texture2D` path still uploads during construction.

**Next Phase 4 work:** move the temporary legacy mesh/texture compatibility uploads behind explicit renderer residency requests so CPU asset construction has no renderer side effects. After that, finish production texture payload policy (mip generation, KTX2/native compression/transcoding selection) without reopening source decoding in runtime.

### Phase 4 explicit compatibility residency checkpoint — 2026-09-03

The remaining constructor/registration upload side effects are now separated from CPU object creation:

- `Texture2D` constructors no longer receive `TextureRuntimeContext` and do not call Vulkan/OpenGL upload code. File-backed construction performs only the still-legacy loose-source stb decode, while memory-backed construction only owns a CPU RGBA copy;
- renderer state is attached only through the private `Texture2D::MakeResident(TextureRuntimeContext)` compatibility operation. `TexturePool` is the owning residency surface and exposes `RequestTextureResidency()`; recursive loading, lazy/source loading, cooked `TextureAsset` adaptation, transient cubemap textures, and manual storage all request residency explicitly after CPU construction and before CPU pixels may be released;
- `MeshPool::RegisterMesh()` and `GetOrCreateAndRegisterMesh()` now create/deduplicate CPU `Mesh` geometry only. They no longer allocate `MeshBufferData`, assign renderer mesh IDs, or call `UploadMeshToMegaBuffer()` as a registration side effect;
- `MeshPool::RequestMeshResidency()` is the only compatibility path in the pool that allocates `MeshBufferData`, assigns the legacy renderer mesh ID, calculates residency AABB data, and uploads into the selected renderer's mega buffer;
- `MaterialPool::RegisterMaterialBinding()` explicitly requests mesh residency when a CPU mesh becomes part of a legacy draw binding. Vulkan's standalone glyph-quad path does the same rather than assuming registration uploaded it;
- the architecture verifier now rejects renderer-context `Texture2D` constructors, implicit texture generation calls, mesh uploads occurring before `RequestMeshResidency()`, renderer-coupled texture construction, or compatibility draw paths that stop issuing explicit residency requests.

This closes the Phase 4 exit criterion **`no asset constructor uploads to a renderer`**. It does **not** claim that `Texture2D`, `MeshPool`, `TexturePool`, `MaterialPool`, or `LegacyRenderBinding` are final residency architecture; they remain engine-owned compatibility surfaces until the RHI/GPU-resource system replaces them.

Validation for this checkpoint:

- `scripts/verify-build-layout.py` passes with the new constructor/registration residency guards;
- a fresh dependency-free CMake tree builds `SwimAssetSystemTests`, `SwimKtx2ContainerTests`, `SwimAssetPublicHeaders`, `SwimAssetCompilerPublicHeaders`, and `SwimHeadlessCoreAssets`, and all runnable targets pass;
- the legacy renderer remains Windows-only, so compile/link validation for the changed `Texture2D`/`TexturePool`/`MeshPool`/`MaterialPool`/Vulkan compatibility code remains the next normal Windows **soft build**. No dependency pin changed in this checkpoint.

### Phase 4 ordinary source-image mip checkpoint — 2026-09-03

Compiler-side PNG/JPEG/WebP cooking now produces runtime-ready mip metadata instead of one base level:

- `SourceImageTextureCompiler` emits a complete `NativeMipData` chain from the decoded base image down to 1x1, with each level represented by an explicit `TextureMipDesc` and packed into one deterministic payload;
- sRGB color textures are converted to linear RGB before filtering and converted back to sRGB after filtering, avoiding the dark/incorrect mip averages produced by averaging encoded sRGB bytes directly. Alpha remains linearly averaged;
- normal-map mips average decoded `[-1, +1]` vectors and renormalize RGB before re-encoding, while data/linear textures use ordinary linear channel filtering;
- the source-image compiler test now checks full-chain dimensions/storage, the known red+green sRGB linear-space average, alpha averaging, normal-vector renormalization, WebP mip generation, and the existing unsupported DDS behavior;
- the static-model compiler profile fingerprint is bumped to `texture=ktx2-or-rgba8-mips-v3`, forcing existing base-level-only cooked source images to recook under the new policy;
- the architecture verifier now requires the mip-chain, sRGB-filtering, normal-map-filtering, and new profile-fingerprint policy.

Validation for this checkpoint:

- the normal offline CMake asset/foundation validation remains green;
- `SourceImageTextureCompiler.cpp` plus `SwimSourceImageTextureCompilerTests` compile and run under GCC/C++20 in this environment using a tiny test-only stb decode shim for the known PNG fixture and the system libwebp decoder for the real WebP fixture. This directly exercises the new mip-generation/filtering code without changing repository dependencies;
- the pinned real stb/libwebp dependency build remains part of the next Windows soft-build validation.

This still leaves **`convert textures to KTX2/native compressed formats` unchecked**. Ordinary source images now have production-shaped mip chains, but the payload is still uncompressed RGBA8. The next texture compiler step is to choose and emit actual distribution/runtime variants (for example BC5 normals plus BC7/appropriate color/data formats on desktop and/or BasisU KTX2 where cross-platform distribution wins) rather than compressing blindly.

### Replace `MeshPool`

Do not create another singleton named `MeshRegistry`.

Create engine-owned asset/runtime services.

A mesh asset should contain CPU/runtime metadata such as:

- vertex stream descriptors;
- packed vertex bytes;
- index bytes/type;
- submeshes/primitives;
- material slot indices;
- local bounds;
- LOD descriptors;
- meshlet descriptors/data where generated;
- skin/morph stream references where applicable. *(2026-09-24, item 78: a second vertex stream with `Joints0`/`Weights0`, and mesh payload version 2 with morph targets and default weights.)*

A mesh asset should **not** contain:

- `VkBuffer`;
- OpenGL buffer IDs;
- permanent mega-buffer offsets;
- renderer pointers;
- upload side effects in its constructor.

### Replace `MaterialData` geometry coupling

Material and mesh identity must be independent.

Target data model:

```text
ModelAsset
  |-- nodes/transforms
  |-- MeshAsset handles
  `-- default MaterialInstance handles per material slot

MeshAsset
  |-- geometry
  |-- submeshes
  `-- material slot indices

MaterialTemplate
  |-- shader/features/schema
  `-- render-state policy

MaterialInstance
  |-- parameters
  `-- texture/sampler handles
```

This allows one mesh to use many materials and one material to be used by many meshes without duplicating ownership.

### Replace `TexturePool` and `Texture2D` side effects

Separate:

1. source image;
2. decoded/compiler image;
3. compiled texture asset;
4. runtime texture metadata;
5. GPU image residency.

A `TextureAsset` handle should not synchronously call stb, allocate Vulkan memory, upload staging data, become bindless, and register itself in a static cleanup set.

### glTF/GLB importer

Use fastgltf in `SwimAssetCompiler` and optionally in a dev-only loose-source importer.

Import:

- [x] nodes/hierarchy;
- [x] transforms;
- [x] meshes/primitives;
- [x] material slots;
- [x] metallic-roughness materials;
- [x] textures/samplers;
- [x] skins/skeletons; *(2026-09-24, item 78: joints, inverse binds, JOINTS_0/WEIGHTS_0 incl. Draco; compiled parents first with remapped joints; [Animation](Animation.md))*
- [x] animation channels; *(translation/rotation/scale/morph weights, step/linear/cubic spline; node-level `weights` arrays hit a fastgltf 0.9 parser defect, see [Animation](Animation.md#gltf-import))*
- [x] morph targets; *(position/normal/tangent deltas, mesh default weights)*
- [ ] relevant cameras/lights if desired;
- [x] deliberately supported extensions. *(Current parser set includes `KHR_mesh_quantization`, `KHR_texture_basisu`, `KHR_texture_transform`, `KHR_draco_mesh_compression`, `EXT_texture_webp`, `MSFT_texture_dds`, and `KHR_materials_unlit`; accepting parser metadata is separate from implementing each extension's codec/material semantics.)*

Then run offline processing:

- [ ] generate missing tangents;
- [x] optimize vertex cache;
- [x] optimize vertex fetch;
- [x] optimize overdraw where appropriate;
- [ ] generate LODs if configured;
- [ ] generate meshlets;
- [ ] pack/quantize runtime vertex formats;
- [ ] convert textures to KTX2/native compressed formats; *(PNG/JPEG/WebP now decode compiler-side into full cooked RGBA8 mip chains and KTX2 sources stay KTX2; final compressed/native payload production and platform-variant selection remain open.)*
- [x] decode Draco only in import/compiler path when source uses it. *(Pinned Draco 1.5.7 is private to the asset-compiler dependency bundle. `GltfImporter` resolves extension attribute IDs, decodes mesh points/faces/attributes, and emits ordinary Swim intermediate geometry; runtime assets and renderer residency contain no Draco types or decoder dependency.)*

### KTX2 runtime texture path

Use KTX2 as the normal compiled texture container.

Support:

- [x] mip chains; *(validated level index metadata with per-mip offsets/sizes/dimensions)*
- [x] sRGB/linear metadata; *(typed KTX2 formats map into backend-neutral color-space metadata; universal-format DFD parsing remains future work)*
- [x] normal-map policy; *(compiler texture semantics distinguish color/normal/data/HDR and normal/data requests remain linear)*
- [x] BC family on desktop where appropriate; *(runtime/compiler metadata covers BC1/3/5/7; actual RHI capability selection/upload is later)*
- [x] ASTC/ETC variants for future mobile; *(metadata variants exist; platform payload production/selection is later)*
- [ ] BasisU transcoding when the distribution strategy benefits from it;
- [x] cubemaps/arrays; *(KTX2 shape/face/layer metadata is preserved and tested, including cube arrays)*
- [ ] HDR environment textures.

### `.sasset` format

The format should be versioned and chunked.

Suggested container fields:

- magic;
- schema version;
- asset type;
- AssetId;
- content hash;
- compiler/profile hash;
- dependency table;
- chunk table;
- per-chunk compression;
- uncompressed size;
- alignment;
- platform/GPU-format variant metadata;
- optional source/debug provenance.

The mesh payload should be deliberately upload-friendly:

```text
Mesh header
Submesh table
LOD table
Vertex stream descriptors
Aligned vertex byte chunks
Aligned index byte chunks
Meshlet metadata/data
Skin/morph streams if present
```

### Direct-to-buffer means bulk-copy friendly, not GPU-address baked

Correct runtime path:

```text
.sasset/.spack range read or mmap
        -> validate
        -> choose compatible payload
        -> allocate GeometryHeap range
        -> allocate upload-ring range
        -> memcpy aligned chunk
        -> transfer queue copy
        -> publish residency handle after timeline completion
```

Do not store permanent Vulkan/D3D12/Metal GPU addresses or heap offsets in the portable asset file.

### `.spack` packages

Provide package files for shipping/streaming:

- [ ] TOC lookup by AssetId;
- [ ] chunk/range addressing;
- [ ] dependency metadata;
- [ ] zstd compression only where useful;
- [ ] uncompressed upload-ready chunks where decompression would be wasted work;
- [ ] checksums/content hashes;
- [ ] memory mapping;
- [ ] patch/version support later.

### Font assets

Font loading should become part of the asset system rather than a process-global recursive scan.

Compiler/tool path can produce:

- font metadata;
- MSDF atlas pages;
- glyph metrics;
- fallback metadata;
- source font reference where runtime shaping requires the font face.

### Phase 4 exit criteria

- [x] a glTF/GLB source model compiles through fastgltf into Swim runtime assets. *(The dev bootstrap performs import -> meshoptimizer -> static-model `.sasset` cook -> normal runtime load; the dependency-enabled end-to-end test is authored and awaits the next Windows/MSVC validation in this checkpoint.)*
- [x] runtime model loading does not require tinygltf/fastgltf object graphs. *(`LoadSasset()` reconstructs Swim CPU asset types/typed handles only; importer types terminate in `SwimAssetCompiler`.)*
- [x] mesh/material/texture are independent asset identities. *(The Phase 4 asset schemas use independent typed handles, and the legacy material/mesh seam no longer models mesh ownership as material state.)*
- [x] no asset constructor uploads to a renderer. *(`Texture2D` CPU construction is renderer-free and mesh registration is CPU-only; the legacy pools issue explicit renderer residency requests only when a compatibility draw/resource actually needs GPU residency.)*
- [x] no asset registry is a process-global singleton. *(`AssetSystem` is engine-owned and authoritative; the remaining renderer pools are engine-owned compatibility residency surfaces that now consume it rather than importing/registering a second source asset graph.)*
- [x] content hashing replaces O(N) raw-byte pool scans for deduplication. *(AssetSystem uses `ContentHash`; transitional mesh and embedded-texture dedup paths now use SHA-256 indices instead of pool-wide byte comparisons.)*

---

## Phase 5 — Scene/ECS cleanup and render-facing data boundaries

Do this before GPU Scene implementation.

### Keep EnTT

There is no architectural reason to replace EnTT.

The work is around ownership and component boundaries.

### Scene ownership

- [x] `SceneManager` owns scenes explicitly. *(The engine-owned `SceneSystem` is the current scene-manager implementation: it owns the catalog, loaded scene instances, runtime `SceneId`s, startup selection, and scene lifecycle.)*
- [x] scenes are not globally preregistered through a static factory. *(The first-party game registers `SandBox` explicitly on the engine-owned `SceneSystem` before startup; static `DEFINE_SCENE`/registrar metadata is removed.)*
- [x] an active scene is an application concept, not a dependency used by low-level components. *(`SwimEngine` selects the active scene during frame orchestration and passes an explicit `Scene*` into Physics and renderer traversal; Physics/OpenGL/Vulkan/VulkanIndexDraw no longer own `SceneSystem*` or call `GetActiveScene()`.)*
- [x] scene identity is explicit. *(Each loaded scene instance receives a monotonic runtime `SceneId`; the application-designated active scene carries both its pointer and ID.)*
- [x] multiple loaded scenes are supported. *(The instance-owned catalog can construct multiple named scene instances in one `SceneSystem`; only the application-designated startup/active scene receives the active update/init path.)*
- [x] headless scenes work. *(`SceneCoreServices` is independently valid without input/camera/render pools/tools; presentation/editor facilities initialize only when the optional presentation profile is present.)*

### Phase 5 explicit scene catalog/identity checkpoint — 2026-09-03

The first scene-ownership cut removes static construction and makes application intent explicit without pretending the renderer-facing boundary is finished:

- Added `SceneCatalog`, an instance-owned descriptor catalog with deterministic insertion order and structured rejection of empty, missing-factory, and duplicate scene registrations. It contains construction metadata only; no live scene is created by static initialization.
- Removed `SceneSystem::Preregister`, the mutable static scene descriptor vector, `SceneRegistrar`, `REGISTER_SCENE`, and `DEFINE_SCENE`. `SandBox` is now an ordinary scene type.
- `main` explicitly registers the game scene type and designates the startup scene before `SwimEngine::Start()`. `SandBox::Awake()` no longer reaches back into `SceneSystem` to make itself active.
- Startup lifetime is explicit: `SwimEngine::Create()` constructs the engine-owned `SceneSystem` before application configuration runs, while `SwimEngine::Init()` only injects runtime services and never recreates it. This preserves all pre-`Start()` `SceneCatalog` registrations and makes the documented startup sequence valid.
- Added `SceneId` as an explicit runtime identity for loaded scene instances. `SceneSystem` assigns a monotonic ID as each live scene is inserted and exposes active/name-to-ID lookup while existing compatibility consumers still use the scene pointer.
- `SceneSystem` can own multiple catalog-created/live scenes concurrently. Startup/active selection is separate from registration/ownership.
- Added `SwimSceneCatalogTests` and Phase 5 verifier guards so static scene registration macros/vectors, scene self-selection, or loss of the explicit application startup registration are caught.

Validation for this checkpoint:

- `SwimSceneCatalogTests` compiles and runs directly under GCC/C++20;
- `scripts/verify-build-layout.py` passes with the new Phase 5 invariants.

That first checkpoint deliberately stopped before the renderer-independent context cut. The immediately following checkpoint completes that ownership seam while keeping the compatibility `SceneSystem` type name until a later naming/API cleanup.

### Phase 5 renderer-independent/headless scene context checkpoint — 2026-09-03

Implemented in the next scene-ownership cut:

- Split `SceneSystemServices` into `SceneCoreServices`, `ScenePresentationServices`, and `SceneToolServices`. Core validity now requires only filesystem, jobs, async IO, assets, frame memory, and engine state; input/camera/cubemap/legacy renderer pools plus command/editor/FPS tooling are optional profiles.
- Made the base Scene lifecycle presentation-optional. CPU scene ownership, entity/behavior lifecycle, transform hooks, BVH state, jobs, assets, and physics can initialize without renderer/input/font/material services; debug draw, gizmos, editor camera, UI handling, and serializer presentation hooks are gated behind available presentation/tool services.
- Removed Vulkan/OpenGL/generic renderer pointers from `Scene` and removed the cached renderer from `Behavior`. The one cubemap demo dependency now receives the backend-neutral optional `CubeMapController` presentation service.
- Removed `SceneSystem*` from Physics. `SwimEngine` chooses the active application scene and drives its explicit `UpdatePhysics()` / `FixedUpdatePhysics()` boundary; `ScenePhysicsBridge` performs ECS synchronization against the generic `PhysicsWorld`.
- Removed `SceneSystem*` and `GetActiveScene()` discovery from both legacy renderers and `VulkanIndexDraw`. `Renderer::SetRenderScene(Scene*)` is the transitional explicit presentation input until Phase 7 render extraction replaces direct ECS traversal entirely.
- Removed `SceneSystem*` from `Scene` and `Behavior`. Scene hotkeys/editor sync use optional command/message callbacks supplied by the tool profile instead of reaching back into the scene manager.
- Extended `scripts/verify-build-layout.py` so a renderer/Physics `SceneSystem*`, renderer/Physics `GetActiveScene()`, flat mandatory scene-presentation services, or renderer pointers in Scene/Behavior are architecture failures.

Validation for this checkpoint:

- `scripts/verify-build-layout.py` passes with the explicit scene-input and headless-context invariants;
- the fresh offline CMake regression continues to pass `SwimSceneCatalogTests`, `SwimAssetSystemTests`, `SwimKtx2ContainerTests`, public asset/compiler header targets, and `SwimHeadlessCoreAssets`.

The remaining Phase 5 scene work is no longer about who owns/selects a scene. It is now the deeper ECS/render-data cleanup: per-scene Transform dirty state, per-view Frustum state, stable persistence IDs, asset-handle-only renderable components, and eventually replacing direct renderer ECS traversal with render extraction.

**Next Phase 5 work:** move Transform dirty queues/versioning out of static component state into a scene-owned Transform system, then replace the static Frustum cache with explicit per-view data.

### Phase 5 scene-owned Transform tracking checkpoint — 2026-09-03

Completed immediately after the scene-context cut:

- Added scene-owned `TransformSystem` mutation tracking with a per-scene dirty queue, frame epoch, dirty flag, and monotonic mutation version.
- Removed the process-global `Transform::DirtyEntities`, `DirtyEpoch`, `TransformsDirty`, and `GlobalMutationVersion` state and the static BeginFrame/query APIs built around them.
- Scene Transform construction/binding now wires both the owning registry and owning `TransformSystem`; hierarchy invalidation continues through that explicit scene-local context.
- `SceneBVH` receives the owning `TransformSystem` directly, so one scene's Transform mutations cannot force/refit another scene's BVH.
- Vulkan scene packet/BVH/transform upload caching now reads the explicit render Scene's transform mutation version and dirty entities instead of one process-global serial/vector.
- `SceneSystem::BeginFrame()` advances every loaded scene's tracker; the engine frame loop no longer clears a process-global Transform dirty list.
- Added `SwimTransformSystemTests` covering per-frame deduplication, independent entity queueing, mutation-version advancement, null rejection, and frame reset behavior. The architecture verifier now rejects any return of the old static Transform dirty state.

This closes the **per-scene Transform dirty state** requirement, but does not yet claim that Transform is a pure local-data component: it still owns world-matrix caches/hierarchy links and the current generic physics interpolation fields, and hierarchy propagation is still recursive rather than a dedicated batch system.

**Next Phase 5 work:** replace the static/global `Frustum` cache with explicit per-view frustum data and pass that view state into renderer/BVH traversal.

### Phase 5 per-view Frustum checkpoint — 2026-09-03

Completed as the next bounded scene/render-data cleanup:

- Removed `Frustum::Get()`, `Frustum::SetCameraMatrices()`, and all process-global cached camera/frustum matrices, revision counters, and movement flags. `Frustum` is now an ordinary independently instantiated view-state object.
- Each Frustum instance owns its previous view-projection matrix, content-derived revision, movement flag, and six normalized planes. The content-derived revision is intentional: shared entity/BVH visibility caches can distinguish two different views even if both view objects have advanced the same number of times.
- OpenGL owns and updates its presentation `viewFrustum`; Vulkan indexed traversal owns and updates its own `viewFrustum`. Neither renderer asks a global Frustum singleton for state.
- `SceneBVH` classification/cache reuse consumes the explicitly supplied `Frustum` and that instance's revision. This keeps BVH state compatible with multiple independent views rather than one process-wide camera.
- Added `SwimFrustumTests` covering independent view revisions/history, unchanged-view reuse, and mutation of one view without altering the other. The test target is defined after legacy EnTT/GLM dependencies are available, alongside `SwimTransformSystemTests`, so non-legacy Linux foundation configure does not reference undeclared legacy dependency targets.
- Extended `scripts/verify-build-layout.py` to reject static/global Frustum APIs/state and require explicit OpenGL/Vulkan Frustum ownership/update plus supplied-Frustum BVH revision consumption.
- During compile-oriented review of the Vulkan conversion, fixed a real use-before-declaration in the GPU-cull reuse stamp path and made the no-render-scene path invalidate reuse safely before returning. The final header-hygiene sweep also made OpenGL's concrete Frustum dependency explicit in `OpenGLRenderer.h`, so PCH/transitive include order cannot hide that contract.

Validation for this checkpoint:

- `scripts/verify-build-layout.py` passes with the per-view Frustum invariants;
- `SwimFrustumTests` compiles and runs under GCC/C++20 against a local GLM API-contract stub in this validation environment;
- a fresh offline CMake tree compiles and runs `SwimAssetSystemTests`, `SwimKtx2ContainerTests`, `SwimSceneCatalogTests`, `SwimMemoryTests`, `SwimJobSystemTests`, `SwimEngineConfigTests`, and `SwimHeadlessCoreAssets`, and compiles the asset/compiler public-header targets;
- the broader offline Platform/IO target is intentionally not used as a compile signal here because `SWIM_OFFLINE_DEPENDENCY_STUBS` does not provide SDL3 headers, so that target stops at `SDL3/SDL_loadso.h` in the unchanged platform implementation. The full legacy renderer remains a Windows/dependency-enabled build target and should receive the normal Windows soft-build validation on a dependency-populated checkout.

### Phase 5 runtime regression hardening checkpoint — 2026-09-03

Windows runtime validation after the scene-service and asset-cooker cuts exposed two integration regressions and one logging-quality issue; all are fixed without starting the next SceneCommandBuffer task:

- Renderer-owned cubemap presentation state is now late-bound **after `Renderer::Awake()` and before `SceneSystem::Awake()`**. The previous service snapshot happened before Vulkan/OpenGL created their `CubeMapController`, permanently injecting `nullptr` into scenes and silently preventing the default sky from initializing. `CubeMapControlTest` now validates all six CPU faces, explicitly loads `Cubemaps/Clean/cubemap_*` as the default preset, enables rendering only after `SetFaces()` succeeds, and logs the selected preset.
- Development asset bootstrap retains a distinct `SourcesSkippedUnsupported` result for genuinely unsupported future authoring features, but Draco is no longer one of them: the later source-codec ownership checkpoint adds compiler-side `KHR_draco_mesh_compression` decode. The checked-in Draco Sponza variant should now cook instead of being classified as unsupported, without adding Draco to runtime residency.
- The spdlog bridge disables `std::cerr`'s standard `unitbuf` behavior while redirected. Without this, every chained `operator<<` insertion flushed the custom stream buffer and produced fragmented one-token error records. The original unit-buffering state is restored during logging shutdown/failure recovery.
- Vulkan frame-in-flight indexing now uses `uint32_t` end-to-end at the renderer boundary, matching descriptor/index-draw APIs and removing the repeated MSVC `C4267` `size_t` → `uint32_t` narrowing warnings from the draw path.
- Verifier coverage now enforces cubemap late-binding order, rejects pre-Awake controller snapshots, requires the compiler-side Draco fixture/decode/bootstrap-success contract, and preserves line-oriented stderr logging.

This closes the current Transform/Frustum state-isolation task at a clean boundary. The next critical-path scene item is **22: replace the remaining scene-owned `EntityFactory` mutation queue with an explicit `SceneCommandBuffer`**, rather than extending this commit into persistence or render extraction.

### Windows runtime-validation hardening checkpoint — 2026-09-03

The first dependency-enabled Windows runs after the Phase 4/5 boundary work exposed startup-path assumptions that Linux foundation tests could not exercise. These are treated as validation fixes, not a new architecture phase:

- The process now initializes `spdlog` before engine startup with both a colored console sink and a timestamped basic file sink under `Logs/` beside the executable. Existing `std::cout`/`std::cerr` diagnostics are bridged into the logger so legacy diagnostics are captured without a disruptive all-at-once logging rewrite. File-sink failure degrades to console-only logging.
- The Windows legacy executable explicitly links as `/SUBSYSTEM:CONSOLE` in every configuration; Release no longer uses a linker pragma that suppresses the console. Top-level startup/runtime exceptions are logged before returning an error code.
- Development asset bootstrap diagnostics now include the resolved asset root, root-model load count, error stage, source path, and message. This makes import/cook/load failures visible in Release and in persistent logs.
- `MaterialPool` compatibility residency no longer throws a failed authoring-asset cook through scene initialization. A failed recook keeps an already resident binding when available; an initial failure returns an empty binding and the demo scene skips that render component. This is deliberately a compatibility-layer policy: the compiler error remains loud in diagnostics instead of being hidden.
- The `webp_sofa.glb` startup failure was traced to the Khronos Sheen Wood Leather Sofa sample requiring `KHR_texture_transform` in addition to `EXT_texture_webp`. The fastgltf supported-extension mask now accepts `KHR_texture_transform`, and the importer regression fixture declares that extension as required so this exact cook rejection cannot return. The current legacy material bridge still does not claim full shader-semantic coverage for sheen/specular or every texture-transform use; those remain renderer/material-system work rather than source-import blockers.
- Build-layout verification now guards the logging dependency/sinks, Release console subsystem, process logging lifetime, non-fatal cooked-model compatibility fallback, and required `KHR_texture_transform` importer capability.

Validation for this follow-up:

- `scripts/verify-build-layout.py` passes with the new startup/logging/importer invariants;
- the dependency-free Phase 4/5 CMake test matrix remains the local validation baseline;
- fastgltf v0.9.0 exposes `Extensions::KHR_texture_transform`, matching the pinned importer API used here;
- the next normal Windows clean/soft build is the authoritative compile/link/runtime validation for the newly added `spdlog` dependency and the real asset set copied beside the executable.

### Phase 4 source-codec ownership / Draco checkpoint — 2026-09-03

Completed the source-format dependency cleanup before returning to scene work:

- Added pinned Draco `1.5.7` as an **asset-compiler-only** dependency with glTF bitstream mode enabled and unrelated point-cloud/tests/plugins/install outputs disabled. fastgltf continues to own glTF structure/extension parsing; Draco owns only compressed geometry decoding.
- Added the explicit `Swim::AssetCompilerDependencies` CMake bundle containing simdjson/fastgltf, meshoptimizer, Draco, compiler-side stb, and libwebp. `SwimAssetCompiler` consumes that bundle privately, making the tool/runtime ownership boundary visible in the target graph instead of scattering codec links across production targets. Draco is reached through the Swim-owned `Swim::AssetCompilerDraco` adapter so both compiler code and fixture tests receive the pinned package's source/generated include roots without leaking those paths into first-party targets.
- `GltfImporter.cpp` now handles `KHR_draco_mesh_compression` by extracting the extension buffer view, decoding it with Draco, resolving the extension semantic-to-unique-ID mapping, reconstructing POSITION/NORMAL/TANGENT/TEXCOORD_0 data and triangle indices, and immediately emitting Swim-owned `SourcePrimitive` data. Draco/fastgltf types terminate in that implementation TU.
- Added deterministic Draco importer and development-bootstrap tests that encode a one-triangle source fixture, require the extension, import/cook it, and verify the resulting cooked model is resident instead of counted as unsupported. The static-model compiler fingerprint now includes `draco=1.5.7`, invalidating roots produced before the decode policy existed.
- WebP remains compiler-only through libwebp. Basis/KTX2 remains supported; the runtime dependency has been renamed `Swim::BasisTranscoder` to make its intentionally transitional purpose explicit. Only the Basis transcoder TU is built at runtime, not encoder/tool code.
- Audited the current dependency graph and recorded exact ownership/pins in Section 4.2. Compiler/import codecs do not appear in `cmake/Dependencies.cmake`; runtime source files contain no fastgltf/Draco/libwebp use. Development auto-cook is the deliberate exception that can pull the compiler dependency closure into a development executable.

Validation in this environment: `scripts/verify-build-layout.py` passes; a fresh offline CMake/Ninja tree builds and runs the dependency-free `SwimEngineConfigTests`, `SwimSceneCatalogTests`, `SwimMemoryTests`, `SwimJobSystemTests`, `SwimAssetSystemTests`, `SwimKtx2ContainerTests`, and `SwimHeadlessCoreAssets`, and compiles both asset public-header targets. The deterministic Draco fixture header was independently checked against the pinned 1.5.7 API contract and its generated glTF JSON was parsed successfully.

The first dependency-enabled Windows clean build then exposed a CMake integration bug before the importer could compile: `draco::draco` linked successfully but did not publish the include roots required by embedded consumers, so `GltfImporter.cpp` could not find `draco/compression/decode.h`. The fix adds `Swim::AssetCompilerDraco`, which links the package target while explicitly exporting `${draco_source_SOURCE_DIR}/src` and the top-level binary root containing generated `draco/draco_features.h`. Both production compiler code and Draco source-fixture tests now consume this adapter. The wrapper also scopes `CMP0148=OLD` only around Draco 1.5.7 so CMake 4.x can satisfy that pinned dependency's legacy `FindPythonInterp` use without a project-level developer warning. Configure-time existence checks and `verify-build-layout.py` guard this package-layout contract. A standalone CMake contract harness was also used locally to mock the pinned package target and compile a consumer including both `<draco/compression/decode.h>` and generated `<draco/draco_features.h>` through `Swim::AssetCompilerDraco`, confirming the transitive include/link seam itself is valid before the next Windows dependency-enabled build.

### Phase 4 Windows asset-pipeline validation automation checkpoint — 2026-09-03

The unresolved Windows gate is now encoded into the supported Windows build workflows instead of relying on a developer to remember a separate test sequence:

- `Invoke-SwimWindowsAssetPipelineValidation` lives in `scripts/windows-build-common.ps1` and is called by both clean and soft Windows builds after the primary engine target has compiled and before the secondary Visual Studio solution tree is generated/refreshed.
- The gate explicitly builds `SwimGltfImporterTests`, `SwimSourceImageTextureCompilerTests`, `SwimDevelopmentAssetPipelineTests`, and `SwimAssetCooker`. This forces the dependency-enabled compiler graph—including the `Swim::AssetCompilerDraco` source/generated include adapter—to compile under the exact Windows toolchain used by the engine build.
- The importer regression runs the deterministic Draco encode/decode fixture and the fastgltf extension fixtures; the source-image regression exercises the real compiler-side WebP decoder/mip path; the development-pipeline regression performs import -> optimize -> `.sasset` cook -> runtime load, including a Draco source. A nonzero result from any of these now fails the normal Windows build.
- When the checkout contains an `Assets` directory with loose `.gltf`/`.glb` sources, the same gate also runs the built `SwimAssetCooker` against that real repository asset root. When no such sources are present, it prints an explicit note that only deterministic fixtures were validated; absence is never silently treated as repository-asset success.
- `scripts/verify-build-layout.py` guards the validation helper, required test/cooker targets, and both Windows build-script calls so this gate cannot accidentally disappear during build-script cleanup.

Local validation for this checkpoint: the architecture verifier passes. The dependency-enabled Linux configure cannot be completed in this isolated environment because the repository dependency cache is empty and outbound CPM downloads cannot resolve, and PowerShell/MSVC are not available here. That is exactly why the Windows run remains a gate rather than being marked complete.

**Gate resolution — 2026-09-03:** the developer confirmed the dependency-enabled Windows build path is already green and supplied the repository `Assets` authoring tree used by the real cooker path. The automated Windows validation remains in place as a regression gate. With that blocker cleared, the implementation proceeded through critical-path items 22, 24, and 26 in the Phase 5 checkpoint below.

### Scene context

A scene may receive a small non-owning context for services it legitimately uses:

```cpp
struct SceneContext
{
    AssetSystem& Assets;
    JobSystem& Jobs;
    PhysicsSystem& Physics;
    DebugSystem& Debug;
};
```

Do not give every component a pointer to the entire engine.

### Remove renderer pointers from Scene

A scene should not store `VulkanRenderer`, `OpenGLRenderer`, or backend-specific renderer state.

Rendering observes/extracts from scenes through a render extraction step.

### Transform system

Move scene-global responsibilities out of each `Transform` instance's static state.

Target:

- [ ] Transform component stores local transform/hierarchy data only.
- [x] `TransformSystem` is scene-owned. *(Each `Scene` owns its own mutation tracker and wires each Transform to that tracker when the component is attached.)*
- [x] dirty queue/versioning is scene-owned. *(Dirty entity queues, frame epochs, and mutation versions live in `TransformSystem`; CPU BVH and Vulkan incremental uploads consume the owning Scene tracker.)*
- [ ] hierarchy propagation is explicit and batchable.
- [x] no transform method discovers the active scene globally. *(Transform hierarchy invalidation is wired to its owning registry by Scene; no Transform code resolves an engine or active scene.)*
- [x] no graphics API branch exists in Transform. *(Clip-depth convention is passed as generic `ClipSpaceDepthRange`; Transform has no Vulkan/OpenGL backend lookup.)*
- [ ] physics interpolation state is either a separate component/system or a clearly generic transform interpolation facility.

### Canonical coordinate and clip-space convention

Choose the convention once so Camera and Transform remain backend-neutral.

Recommended modern convention:

- right-handed world space;
- depth range 0..1;
- a consistent screen/UI origin defined by UI, not by graphics API;
- renderer controls viewport orientation/front-face handling;
- adopt reverse-Z when the modern depth pipeline is established, before HZB/occlusion code depends on depth semantics. *(Decided 2026-09-23: the modern renderer uses reverse-Z with `D32Float`, clear 0, `GreaterEqual` and infinite-far perspective; see `Renderer/Visibility/DepthConvention.h` and [GPU visibility](GpuVisibility.md#depth-convention-the-item-50-gate).)*

Vulkan, D3D12, and Metal should adapt at the RHI/backend boundary rather than modifying Camera math. Legacy OpenGL 4.6 can use clip-control/backend adjustments where necessary.

### Camera/view redesign

A single global camera/frustum is insufficient.

Create:

- [ ] `CameraComponent` or reusable Camera data;
- [ ] `RenderView`/`ViewDesc`;
- [x] per-view frustum; *(OpenGL and Vulkan traversal own independent Frustum instances; BVH queries consume the supplied instance/revision.)*
- [ ] current/previous view-projection;
- [ ] viewport/scissor;
- [ ] jitter;
- [ ] exposure;
- [ ] layer/mask policy;
- [ ] camera-cut/history-reset flag.

This naturally supports game view, editor view, shadows, portals, mirrors, cubemap capture, and multiple windows.

### Entity mutation queue

Replace global `EntityFactory` queues with scene-owned mutation/command buffers.

```text
SceneCommandBuffer
  |-- CreateEntity
  |-- DestroyEntity
  |-- Add/RemoveComponent
  `-- custom deferred scene mutation
```

This fixes active-scene ambiguity and provides a clearer threading boundary.

### Behavior registry

The behavior concept can remain.

Change:

- [x] `BehaviorRegistry` is owned by runtime/tool context rather than a mutable process-global singleton. *(The engine-owned `SceneSystem` owns one deterministic registry and injects it into its Scene instances; the old singleton factory/registrar path is removed.)*
- [x] behavior factory registration remains data-driven. *(Application code explicitly registers named behavior descriptors/factories before startup; descriptor order is deterministic and duplicate/invalid registration is rejected.)*
- [x] behaviors receive explicit scene/service context. *(Behavior construction receives its owning `Scene*`; commonly used optional input/camera services are refreshed from that scene and no SceneSystem/renderer locator is cached.)*
- [x] behaviors do not cache shared ownership of core services by default. *(Behavior caches are non-owning raw pointers to the owning scene/components/optional presentation services.)*
- [x] behavior execution is a gameplay/scene phase, never renderer traversal. *(Scene lifecycle owns behavior Awake/Init/Update/FixedUpdate/Exit; renderer traversal only observes renderable data.)*

### Scene catalog and construction

Replace static construction/preregistration with explicit descriptors or factory functions.

A game/module should be able to register scene types without constructing live scene instances before the Engine exists:

```cpp
SceneCatalog Catalog;
Catalog.Register("Sandbox", [] (SceneCreateContext& Context)
{
    return std::make_unique<SandboxScene>(Context);
});
```

Requirements:

- [x] no mutable static vector of live scenes; *(scene descriptors now live in an instance-owned `SceneCatalog`; the old static preregistration vector/macros are gone.)*
- [x] no scene constructor depends on a global Engine; *(preserved from the Phase 2 service-injection migration; scene construction receives no global engine locator.)*
- [x] scene type registration is deterministic and testable; *(`SceneCatalog` preserves explicit registration order and rejects empty/duplicate descriptors; `SwimSceneCatalogTests` covers the contract.)*
- [x] a `SceneId`/`SceneHandle` identifies loaded scene instances; *(`SceneId` is assigned when a live scene is inserted and is exposed for active/name lookup.)*
- [x] multiple loaded scenes are legal even if an application designates one as the primary gameplay scene; *(`main` explicitly selects `SandBox` as startup while `SceneSystem` ownership is not restricted to one loaded instance.)*
- [ ] tools may create isolated scene instances with their own service/context scope.

### Scene persistence and tooling separation

Introduce a durable scene identity model separate from EnTT's runtime handles.

Suggested concepts:

- `SerializedEntityId` or `EntityGuid` for persistent cross-reference;
- `AssetId` for model/material/texture/animation references;
- schema/version metadata for scene documents;
- explicit component serializers/deserializers;
- optional human-readable JSON authoring representation;
- compiled/package representation later where loading performance warrants it.

Separate the flow:

```text
Scene/ECS
   |
   v
SceneSerializer <---- component serialization registry
   |
   +----> SceneStorage ----> filesystem / package / memory
   |
   `----> ToolingBridge ---> editor IPC / in-process tools / future transport
```

Rules:

- [x] serializer code does not call `WM_COPYDATA`, open files, or locate executable directories; *(`SceneSerializer` only observes ECS/identity/component data and emits JSON.)*
- [x] storage does not know about editor commands; *(`SceneStorage` owns only filesystem persistence and structured save results.)*
- [x] editor transport does not own scene serialization policy; *(`SceneToolingBridge` is only a message callback boundary; `SceneSyncTracker` composes it with `SceneSerializer`.)*
- [x] parent/child references serialize stable entity IDs, not integral `entt::entity` values; *(Every Scene entity receives `SerializedEntityId`; editor commands and parent serialization resolve through the scene identity map.)*
- [x] asset references serialize `AssetId` plus optional debug/source provenance rather than relying on fuzzy pool names; *(Composite model, mesh, and material bindings carry `AssetId`; legacy source paths remain optional debug provenance only.)*
- [x] runtime-only entities/components can opt out explicitly; *(`DoNotSerialize` is an explicit marker in addition to the existing editor-tag compatibility filter.)*
- [ ] unknown/newer component data fails or degrades with structured diagnostics instead of silently corrupting a scene.

### CPU BVH

Keep and clean the existing CPU BVH.

It remains useful for:

- raycasts/selecting entities;
- editor/debug queries;
- CPU spatial gameplay queries;
- streaming heuristics;
- renderer validation/reference culling;
- possible coarse GPU-scene organization experiments.

It is **not** the required source of the final GPU visible list.

### Phase 5 exit criteria

- [x] Transform dirty state is per scene. *(`Transform` no longer contains static dirty queue/epoch/mutation state; `Scene::TransformSystem` owns and resets it per loaded scene.)*
- [x] Frustum/view state is per view. *(The static Frustum cache/API is removed; renderer traversal owns explicit Frustum instances with independent view history/revisions.)*
- [x] Scene has no Vulkan/OpenGL renderer pointer. *(Scene stores no renderer/backend pointer; optional presentation needs are backend-neutral services such as `CubeMapController`.)*
- [x] components do not discover the active scene through a global engine. *(First-party component/behavior code contains no active-scene/global-engine discovery; application scene selection is centralized in `SwimEngine`/`SceneSystem`.)*
- [ ] renderable components contain asset/render handles, not GPU objects.
- [x] multiple scenes can exist without shared transform/frustum globals. *(Transform mutation state is owned by each Scene and Frustum state is owned by each render view/traversal.)*
- [x] scene types register without constructing live scenes during static initialization. *(Scene types are registered explicitly into the instance-owned `SceneCatalog`; static scene registrar macros are removed.)*
- [x] persisted entity references use stable serialization IDs rather than raw EnTT values. *(`EntityIdentityMap` provides monotonic IDs plus explicit rebinding for future document restore; editor traffic uses the same IDs.)*
- [x] scene asset references use `AssetId`. *(Model/mesh/material persistence emits stable asset IDs, with source path only as optional provenance.)*
- [x] scene serialization, storage, and editor transport are independent modules. *(`SceneSerializer`, `SceneStorage`, `SceneToolingBridge`, and `SceneSyncTracker` replace the monolithic `SerializedSceneManager`.)*

### Phase 5 command/persistence/convention checkpoint — 2026-09-03

This checkpoint deliberately closes the remaining scene-foundation items before generic physics contracts begin:

- Replaced the old `EntityFactory` create/destroy queues with a scene-owned `SceneCommandBuffer` built on a thread-safe move-only `DeferredCommandBuffer<Scene>`. All commands share one FIFO stream, commands added while a flush is executing are deferred to the next flush, recursive flushes are rejected, and scene exit clears pending work. Editor scene mutations now enter this same boundary instead of mutating the registry from command callbacks.
- Removed the process-global/static behavior factory/registrar path. `SceneSystem` owns a deterministic `BehaviorRegistry`, the application explicitly registers behavior factories, and named behavior removal now resolves through registry descriptors rather than remaining an editor TODO.
- Added `SerializedEntityId` plus scene-owned `EntityIdentityMap`. Runtime EnTT handles no longer cross the editor/persistence boundary; create/destroy/add/remove/material/behavior editor commands consume 64-bit stable IDs, and `CreateEntityWithSerializedId()` provides the restore seam needed by a future scene loader.
- Replaced `SerializedSceneManager` with four independent pieces: `SceneSerializer` (pure ECS -> document), `SceneStorage` (filesystem only), `SceneToolingBridge` (transport callback only), and `SceneSyncTracker` (stable-ID delta composition). **Historical note:** that external-editor/scene-JSON experiment is now dormant and no longer runtime-wired; the split files remain buildable/reference-only rather than participating in Scene lifecycle or transform dirtiness.
- Added explicit `DoNotSerialize` persistence opt-out. Parent references serialize `SerializedEntityId`; model/mesh/material references serialize `AssetId`; legacy source paths are optional debug provenance rather than persistence identity.
- Established one backend-neutral camera convention: right-handed world/view space, +Y-up NDC, and 0..1 depth. `Camera` now uses `glm::perspectiveRH_ZO` and contains no graphics-backend branch. OpenGL 4.6 adapts with `glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE)`; Vulkan adapts framebuffer orientation with a negative-height viewport instead of mutating the projection matrix. The existing UI coordinate system remains explicitly bottom-left. Reverse-Z remains a later renderer-depth migration and is not implied by this checkpoint.
- Added/extended regression targets for deferred commands, durable entity identity, behavior registration, and render conventions. `scripts/verify-build-layout.py` now rejects reintroduction of the old factories/serializer, direct gameplay Scene registry creation, raw EnTT IDs in tooling/persistence, backend-dependent Camera projection math, or loss of the serializer/storage/tooling split.

Validation in this environment: `scripts/verify-build-layout.py` passes; `SwimDeferredCommandBufferTests` compiles/runs directly under GCC/C++20; the dependency-free CMake tree configures and the portable EngineConfig/SceneCatalog/DeferredCommandBuffer/Memory/Jobs/Assets/KTX2/public-header regression subset builds and runs. The known offline SDL stub limitation still prevents building Platform-dependent targets such as `SwimAsyncIoTests` without real SDL headers. The new EnTT/GLM legacy tests are represented in the normal Windows dependency-enabled build and are intentionally not claimed as executed in this Linux dependency-stub environment.

**Next critical-path work:** items 27 and 28 are completed by the Phase 6 checkpoint below; item 29 is the Jolt backend baseline plus shared PhysX/Jolt parity run.

---

## Phase 6 — Physics abstraction with PhysX and Jolt parity baseline

The physics seam should be proven before more gameplay becomes dependent on PhysX details.

### Public physics concepts

Create Swim-owned types:

- [x] `PhysicsSystem`
- [x] `PhysicsWorld`
- [x] `BodyHandle`
- [x] `ShapeHandle`
- [x] `PhysicsMaterialHandle`
- [x] `ConstraintHandle`
- [x] `CharacterHandle`
- [x] `BodyDesc`
- [x] `ShapeDesc`
- [x] `PhysicsMaterialDesc`
- [x] `CollisionLayer`
- [x] `RaycastHit`
- [x] `SweepHit`
- [x] `OverlapHit`
- [x] `CollisionEvent`
- [x] `TriggerEvent`

### Rigidbody component

Target component:

```cpp
struct RigidBodyComponent
{
    BodyHandle Body;
    MotionType Motion = MotionType::Dynamic;
    bool UseGravity = true;
    float Mass = 1.0f;
};
```

No `PxRigidActor*`, `PxShape*`, `JPH::BodyID`, or backend object pointer belongs in the generic component.

### Backend interface

The selected backend owns its internal world/body tables.

```text
PhysicsSystem
    |
    +-- PhysicsPhysXBackend
    `-- PhysicsJoltBackend
```

Runtime selection happens from `EngineConfig`.

### Baseline parity required before calling the abstraction proven

Both backends should implement the same integration tests for:

- [x] static bodies;
- [x] dynamic bodies;
- [x] kinematic bodies;
- [x] box/sphere/capsule;
- [ ] convex mesh;
- [ ] triangle mesh;
- [x] gravity;
- [x] mass/damping;
- [x] forces/impulses;
- [x] velocity;
- [x] collision filtering;
- [x] triggers;
- [x] raycast;
- [x] sweep;
- [x] overlap;
- [x] collision events;
- [ ] fixed-step simulation;
- [ ] transform interpolation;
- [x] safe destruction during/around simulation.

### Cooking

Collision cooking is an asset/compiler responsibility where possible.

- [ ] model import can produce collision data;
- [ ] convex/triangle cooked payloads can have backend-specific compiled variants;
- [ ] runtime does not synchronously cook expensive collision geometry on the main thread by default.

### Jobs

Where practical, physics backend worker integration should use or cooperate with the engine jobs system. Do not force identical internal scheduling if a backend has a better native strategy; keep the integration seam explicit.

### Phase 6 generic-physics / PhysX-backend checkpoint — 2026-09-03

Implemented in this checkpoint:

- Added generational Swim-owned `BodyHandle`, `ShapeHandle`, `PhysicsMaterialHandle`, `ConstraintHandle`, and `CharacterHandle` identities plus backend-neutral body/shape/material/world descriptors, motion/force enums, collision layers, query hits, collision events, and trigger events.
- Rebuilt `PhysicsSystem` as a generic backend owner/factory and `PhysicsWorld` as a pure backend-neutral world facade. Neither layer includes EnTT or PhysX/Jolt implementation types.
- Reduced `Rigidbody` runtime state to a generic `BodyHandle` plus serializable/gameplay-facing configuration. Collision layer/mask data is backend-neutral as well.
- Split scene synchronization into `ScenePhysicsBridge`, which owns EnTT/Transform/Rigidbody synchronization while consuming only `PhysicsWorld`. Static bodies push Transform -> physics, kinematic bodies use targets, dynamic bodies pull poses back for interpolation, and shape/material/body resources are scene-local.
- Moved PhysX implementation storage behind `IPhysicsBackend` / `IPhysicsWorldBackend` in `Source/Engine/Systems/Physics/Backends/PhysX` and the dedicated `Swim::PhysicsPhysX` target. `Swim::Physics` does not link PhysX; `SwimEngine` composes the backend through `CreatePhysXBackend()`.
- Added PhysX translations for static/dynamic/kinematic bodies, box/sphere/capsule shapes, materials, gravity/mass/damping, force/impulse/velocity operations, layer/mask filtering, raycast/sweep/overlap, collision/trigger callbacks, fixed-step simulation, and safe logical body-handle invalidation while native actor destruction is deferred until simulation fetch completes. Body creation also treats `PxRigidActor::attachShape()` failure as a hard creation failure and releases the native actor before it can enter the scene or generic handle tables. Convex/triangle runtime binding remains intentionally blocked on the later compiled collision-cooking work.
- Added a backend-independent `PhysicsBackendContract` test suite. The PhysX test instantiates the backend factory and runs the same contract that the Jolt test will use, covering body types, primitive shapes, filtering/queries, gravity, impulses, collision/trigger events, kinematic targets, and stale-handle generation after in-flight destruction.
- Added `SwimPhysics`, `SwimPhysicsPhysX`, `SwimPhysicsPublicHeaders`, `SwimPhysicsHandleTests`, and `SwimPhysicsPhysXBackendTests` CMake targets. Windows clean and soft workflows now build the public-header/handle/backend contract targets and execute the handle + PhysX contract tests before reporting success.
- Extended `scripts/verify-build-layout.py` so PhysX/Jolt leakage into generic physics, backend pointers in Rigidbody, EnTT dependencies in `PhysicsWorld`, raw PhysX linkage from `SwimEngine`, lost backend tests, loss of the `ScenePhysicsBridge` boundary, removal of the checked `attachShape()` unwind path, or narrowing callback actor resolution back to `PxRigidActor*` fail architecture verification.
- The first dependency-enabled MSVC build of this checkpoint exposed that PhysX contact/trigger callback records provide `PxActor*` while `ResolveBody()` originally accepted only `PxRigidActor*`. `ResolveBody()` now accepts the PhysX base actor type, rejects null/non-rigid actors through `PxBase::is<PxRigidActor>()`, and only then performs the rigid-actor handle lookup. This keeps casts out of callback code and makes unsupported actor kinds resolve safely to an invalid `BodyHandle`.
- The following dependency-enabled MSVC clean build progressed past the PhysX backend and exposed a renderer-header self-containment bug: `VulkanIndexDraw.h` forward-declared `TransformSpace` but used `TransformSpace::World` in default member initializers. The header now includes `Engine/Components/Transform.h` directly and no longer forward-declares `TransformSpace`; architecture verification enforces that dependency so PCH/include order cannot hide the error again.
- The next dependency-enabled MSVC clean build progressed beyond both prior fixes and exposed a scene-storage dependency mismatch: `SceneStorage.h` included `nlohmann/json_fwd.hpp`, but Swim deliberately pins/downloads only nlohmann/json's single `json.hpp` release header. `SceneStorage.h` now includes `nlohmann/json.hpp`, matching the repository dependency contract, and architecture verification rejects any future first-party `json_fwd.hpp` include so split-header assumptions cannot reappear.
- The following dependency-enabled MSVC clean build progressed past the JSON fix to roughly 693/716 and exposed EnTT empty-type optimization at the scene wrapper boundary: `registry.emplace<DoNotSerialize>()` returns `void` because `DoNotSerialize` is an empty tag, while `Scene::EmplaceComponent<T>()` still forced every insertion into `T&`. `Scene::AddComponent` and `Scene::EmplaceComponent` now use `decltype(auto)` and branch on the actual `decltype(registry.emplace(...))`, preserving references for ordinary components while correctly returning `void` for optimized tags. The verifier rejects restoring the unconditional `T&` assumption.
- The next dependency-enabled MSVC clean build completed the entire engine compile/link (`716/716`) and entered the automated Phase 4 asset validation gate. `SwimGltfImporterTests` and `SwimSourceImageTextureCompilerTests` passed, but `SwimDevelopmentAssetPipelineTests` terminated with Windows access violation `0xC0000005`. The failure was in the test's lifetime assumptions, not in importer/cooker execution: it cached a raw `ModelAsset*` returned by `AssetSystem::Resolve()` across a second bootstrap, while `RunDevelopmentAssetBootstrap()` republishes the cooked graph and `AssetSystem::Publish()` replaces the owned residency under the same stable handle. The fixture now copies the stable `AssetHandle<ModelAsset>` / `AssetHandle<MeshAsset>` identities, re-resolves the model after every bootstrap/recook boundary, and verifies that dependency handles remain stable. `AssetSystem::Resolve()` now documents that its pointer is a transient residency view and must be reacquired after publish/unload/fail/forget/replacement operations; the core asset-system regression also verifies that a stable handle resolves the replacement residency after republish.
- The following dependency-enabled MSVC clean build confirms that the asset-lifetime fix is correct: the engine again links at `716/716`, all three Phase 4 validation tests run, `SwimAssetCooker` cooks the repository `Assets` root successfully, and the complete Phase 4 gate reports passed. The build then reaches the Phase 6 backend contract and exposes a pure C++ contract-test error: `PhysicsHandle` intentionally provides an explicit `operator bool()`, so direct calls such as `RequirePhysics(triggerShape, ...)` cannot implicitly convert a `ShapeHandle`/`BodyHandle` to the helper's `bool` parameter. Every direct handle assertion now uses `.IsValid()`, preserving the explicit-handle API contract while remaining portable across MSVC/GCC/Clang.
- Linux validation is strengthened so this class of error is no longer hidden behind the Windows-only legacy runtime gate. GLM is now initialized as a cross-platform foundation dependency, generic `Swim::Physics` is defined before the `SWIM_BUILD_LEGACY_ENGINE` early return, and `SwimPhysicsBackendContractCompile` compiles the backend-independent contract header as part of normal dependency-enabled foundation builds. The concrete PhysX backend remains Windows/MSVC-only until the Jolt/backend platform work is completed, but Linux now compiles the same generic physics API and shared backend contract instead of skipping them entirely.

Validation in this environment: `scripts/verify-build-layout.py` passes with regression guards for the PhysX callback actor type, Vulkan `TransformSpace` header self-containment, single-header nlohmann/json dependency contract, EnTT empty-tag insertion, explicit physics-handle validity checks, and the new Linux/foundation placement of generic physics plus the shared backend-contract compile target. The dependency-stub CMake/Ninja tree builds and executes the portable EngineConfig, SceneCatalog, DeferredCommandBuffer, Memory, Jobs, AssetSystem, KTX2, PhysicsHandle, and HeadlessCoreAssets targets successfully. In addition, GCC/C++20 syntax-checks every generic physics `.cpp` and `PhysicsBackendContractCompile.cpp` successfully using a temporary local GLM compatibility stub solely for this isolated validation environment. A true dependency-enabled Linux configure cannot run inside this container because outbound GitHub DNS is blocked and the repository dependency cache is empty; this is an environment limitation rather than a CMake platform gate. The latest real Windows clean build now independently confirms the entire engine link and complete Phase 4 asset gate are green; the only reported blocker in that run is the Phase 6 explicit-handle test compile issue fixed above.

**Historical checkpoint closure:** at this 2026-09-03 checkpoint, items 22 through 28 were complete and item 29 was still pending. Item 29 was subsequently completed by the Jolt parity checkpoint documented below; the current implementation snapshot at the top of this guide is authoritative.

**Historical next step at that point:** item 29 was to add `Swim::PhysicsJolt`, select it through the existing runtime physics factory/configuration seam, and run `PhysicsBackendContract` unchanged against both backends. That work is now complete.

### Test-suite consolidation checkpoint — 2026-09-04

The engine had accumulated roughly twenty `EXCLUDE_FROM_ALL` test executables plus six header-boundary object libraries, each with its own `add_executable`, link list, and `main()`. Adding a test meant editing `CMakeLists.txt`, and only five of those binaries were ever actually executed by a build script, so most coverage silently rotted.

Implemented in this checkpoint:

- Added a first-party test framework at `Source/Tests/Framework/`: a self-registering `TestRegistry`, a thread-safe per-case `TestContext`, non-eliding `SWIM_CHECK*`/`SWIM_REQUIRE*` macros, and a CLI runner with filtering, listing, exclusion, shuffling, repetition, stop-on-failure, and JUnit XML reporting.
- Collapsed every runnable test into one `SwimTests` program. The former per-module `main()` bodies were split into named cases, so the corpus went from about twenty opaque binaries to a little over one hundred individually addressable cases.
- Reorganized `Source/Tests/` into `Framework/`, `Suites/<dependency group>/`, `Fixtures/`, and `HeaderBoundary/`. Suite sources are globbed per group in `cmake/Tests.cmake`, so adding a test is a new file and never a CMake edit.
- Kept the header-boundary gates as separate object libraries and gave them a one-line `swim_add_header_boundary()` declaration. Their narrow link surface is the guarantee; `SwimTests` links everything and cannot provide it.
- Split the shared physics backend contract into four independent scenarios (`RunPhysicsWorldLifecycleContract`, `RunPhysicsSceneQueryContract`, `RunPhysicsSimulationContract`, `RunPhysicsTriggerContract`), each building its own world from a shared fixture. A Jolt backend will register four cases and reuse the fixture unchanged.
- Replaced `Invoke-SwimWindowsAssetPipelineValidation` and `Invoke-SwimWindowsPhysicsValidation` with `Invoke-SwimWindowsTestSuite` (build and run the whole suite plus every boundary gate) and `Invoke-SwimWindowsAssetCookValidation` (cook the real repository asset root). Both Linux build scripts now build and run `SwimTests` too.
- Merged the asset cooker into the asset-compiler module: `Source/Tools/AssetCooker/Main.cpp` moved to `Source/Tools/AssetCompiler/Cli/AssetCookerMain.cpp`, and the library glob excludes `Cli/`. The two CMake targets remain because a static library cannot own a `main()`, but there is now one module and one cook implementation.
- Ignored `Assets/Cooked/` in Git. It is reproducible build output of the authoring tree beside it.

Two latent defects surfaced immediately once everything actually ran:

- Most existing tests used `assert()`, which is a no-op in this project because `NDEBUG` is defined in every configuration, including Debug. Those checks had not been verifying anything. All of them are now `SWIM_CHECK*`/`SWIM_REQUIRE*`.
- `JobSystemTests` asserted that `RegisterCurrentExternalThread()` succeeds while configuring `JobSystemDesc::ExternalThreads = 0`, and called it from the scheduler's own owning thread. That test target was never executed by any build script. It is now two cases that register from a genuine external thread and assert both the reserved-slot success path and the no-slot rejection path.

Validation in this environment: `scripts/verify-build-layout.py` reports no new findings, and a Windows Ninja/MSVC Release build of `SwimTests`, all six header-boundary gates, and `SwimAssetCooker` succeeds. `SwimTests` runs 106 cases across 28 suites with 1067 checks, all passing.

**Next work in this area:** wire `SwimTests --report` into whatever CI runner is adopted, and add suites as the RHI/render phases land rather than adding targets.

### Playing-performance / editor-runtime cleanup checkpoint — 2026-09-04

A ~73-second Visual Studio diagnostic capture showed the Sandbox behaving normally while paused, then collapsing to roughly 30 FPS as Playing began. GPU utilization remained low while process CPU rose sharply, pointing at CPU-side scene/update handling rather than rendering fill-rate or PhysX compute saturation. The Sandbox stress fixture contains 9,261 render entities, roughly half with `Spin`, plus 343 primitive physics bodies, so any accidental per-transform full-scene work becomes catastrophic immediately.

Implemented in this checkpoint:

- **External editor IPC is completely unwired from runtime.** `SwimEngine` no longer includes/owns/initializes `EditorIpcBridge`, does not provide editor-message/connection callbacks to `SceneSystem`, and does not route engine command status through `WM_COPYDATA`. `EditorIpcBridge.*` remains in-tree, explicitly marked legacy/dormant. Future editor work is internal engine UI.
- **Automatic scene JSON persistence/synchronization is completely unwired from runtime.** `Scene` no longer includes, constructs, owns, saves through, flushes, or resets `SceneSerializer`, `SceneStorage`, `SceneToolingBridge`, or `SceneSyncTracker`. Component construction/destruction, hierarchy changes, tag changes, and Transform dirtiness perform no serialization notification checks. The modules were initially retained as dormant reference code; the 2026-09-05 retirement checkpoint moves them outside `Source/` into the non-buildable historical archive (see §0.3).
- Removed legacy editor-message callbacks from `MaterialPool`; material registration/flush no longer has an external-editor side channel.
- Kept durable `SerializedEntityId`/`EntityIdentityMap` because stable engine-side identity remains useful independently of JSON or external tooling.
- Kept the earlier Playing hot-path fixes: behavior initialization is fused into the actual behavior Update/FixedUpdate traversal; physics Transform writes do not emit redundant EnTT patches; and SceneBVH dirty ancestors are refit in batches rather than recomputing shared parent chains once per leaf.
- Added **SceneBVH-owned topology and wide-bounds revisions**. Vulkan GPU culling no longer treats every Transform mutation as a reason to rebuild/copy the complete wide-BVH CPU snapshot. Per-instance Transform uploads still use the scene dirty list, while BVH node uploads occur only when SceneBVH topology or conservative wide bounds actually change. This is particularly important for thousands of rotating stress entities whose transforms mutate every frame but whose fat BVH bounds usually remain valid.
- `scripts/verify-build-layout.py` now enforces the dormant-editor boundary and rejects reintroduction of Scene-owned serializer/sync objects, `SwimEngine` IPC ownership, MaterialPool editor callbacks, active registration of the old external-editor command protocol, or Transform-mutation-driven full GPU-BVH snapshot refreshes.

Validation in this environment: architecture verification passes. A clean Linux GCC 14/CMake/Ninja dependency-stub build configures the consolidated test program and `SwimHeadlessCoreAssets`; `SwimTests` runs **49 cases across 14 suites with 218 checks, all passing**. The full legacy renderer/PhysX runtime is still Windows-only in current CMake, so the repaired Sandbox FPS must be measured on the next Windows run rather than guessed from this container.

**Runtime editor policy:** do not reconnect the old process bridge or automatic scene JSON pipeline while building editor features. New hierarchy/property/component inspectors, gizmos, asset browsers, and scene tools belong in the engine process and should use direct typed engine APIs/state. A future deliberate persistence format may reuse ideas from the dormant serializer, but it is not part of the current frame/update path.
- **Legacy editor hard-off boundary:** the old `ExternalParent` embedding route is now ignored by `SwimEngine`; `EditorIpcBridge.cpp` plus the old scene serializer/storage/sync implementation `.cpp` files are excluded from active runtime targets while their source/header files remain in-tree as historical reference. The legacy SceneSystem editor-message command implementation is compile-disabled. Generic `ExternalWindow` remains a platform capability unrelated to the retired editor.
- **Current editor policy:** no external-process IPC, no automatic scene JSON save/load, and no JSON delta synchronization participate in runtime execution. Future hierarchy, inspector, gizmo, asset-browser, and scene-authoring features are implemented inside Swim Engine's own UI and mutate typed in-process engine/ECS state directly.


### Phase 6 Jolt parity checkpoint — 2026-09-04

This checkpoint implements critical-path item 29 without revisiting the current BVH/scene/Transform-dirty design that later GPU Scene/RHI work is expected to replace.

Implemented in this checkpoint:

- Added pinned Jolt Physics `v5.6.0` acquisition and a private `Swim::Jolt` dependency boundary. `Swim::PhysicsJolt` is a first-party static backend target that exists before the Windows-only legacy-runtime gate, so Linux foundation builds exercise the same Jolt backend code instead of compiling only the generic interface. Jolt's DX12/Vulkan/Metal/CPU-compute, samples, viewer, profiler, debug-renderer, object-stream, install, and upstream test/tool paths are disabled for this consumer.
- Added `CreateJoltBackend()` and runtime `PhysicsBackend::Jolt` composition through the existing `EngineConfig` seam. No gameplay/scene source branches on Jolt or includes Jolt implementation headers.
- Implemented the generic baseline in `JoltWorldBackend`: generational material/shape/body handles; static/dynamic/kinematic bodies; box/sphere/capsule shapes including local poses; gravity, mass, damping, velocities, force/impulse/acceleration/velocity-change modes; kinematic targets; deferred body destruction around an in-flight step; layer/mask filtering; raycast, sweep, overlap; collision events; and trigger enter/exit events.
- Preserved Swim's 32-bit `CollisionLayer` contract rather than narrowing gameplay data to Jolt's native `ObjectLayer`. A backend-owned registry maps unique Swim layer/mask/motion tuples to native object layers, and Jolt's broadphase/object-pair filters reject impossible/static-static or mask-incompatible pairs before narrow-phase contact work. Queries use the same symmetric Swim layer/mask policy through both native object-layer and body filters.
- Kept Jolt allocation/update lifetime explicit: the Jolt global Factory/type-registration lifetime is reference-counted across backend instances, each world owns its temporary allocator, worlds remove/destroy their bodies before backend teardown, and `PhysicsSystem::Update` error bits are propagated through `FetchResults()` instead of silently reporting success.
- Fixed query transform semantics against the Jolt 5.6 API: shape sweeps start from a world transform through `RShapeCast::sFromWorldTransform`, while overlaps explicitly convert the query shape's world pose to the center-of-mass transform required by `NarrowPhaseQuery::CollideShape`. The shared backend contract now includes a non-identity query-shape local pose so this distinction is regression-tested on both PhysX and Jolt.
- Registered the existing four shared parity scenarios for Jolt (`WorldLifecycle`, `SceneQuery`, `Simulation`, `Trigger`) without backend types entering the fixture. Added a Jolt-specific lifetime case that initializes two backend instances, shuts one down, and verifies the surviving backend can still create a world. `cmake/Tests.cmake` includes the Jolt suite whenever `SwimPhysicsJolt` exists, while the generic contract header-boundary gate continues to link only `Swim::Physics`.
- Intentionally **did not** add runtime convex/triangle source-mesh cooking. Those `ShapeType` paths reject unsupported data until the asset compiler can emit backend-specific cooked collision payloads; that is the later cooking checkpoint and is preferable to creating a temporary runtime importer/cooker path now. Fixed-step orchestration and render interpolation likewise remain later engine policy rather than being hidden inside one backend.
- Extended `scripts/verify-build-layout.py` so the architecture gate understands multiple concrete physics backend directories, requires the Jolt dependency/target/runtime/test wiring, verifies the early object-layer/query filtering and overlap COM-transform path, and continues rejecting PhysX/Jolt implementation leakage into generic physics.

Validation completed in this execution environment: `scripts/verify-build-layout.py` passes after the new backend/lifetime/query guards. A fresh dependency-stub Linux configure/build completes and `SwimTests` runs **46 cases across 13 suites with 209 checks, all passing**. The Jolt implementation was cross-checked against the official **Jolt v5.6.0** headers for the `PhysicsSystem`, `BodyInterface`, `BodyCreationSettings`, `BodyFilter`/object-layer filters, `ContactListener`, primitive shapes, ray casts, shape casts, overlap `CollideShape`, job system, allocator, and update-error APIs. This container has no outbound dependency fetch and contains no Jolt source cache, so a real `SwimPhysicsJolt` compile/link/runtime execution cannot be truthfully claimed here; the normal dependency-enabled Linux/Windows build is wired to make those Jolt parity cases part of `SwimTests` rather than silently skipping them.

**Checkpoint closure:** item 29's implementation and regression wiring are complete. Convex/triangle collision cooking, fixed-step orchestration, and transform interpolation remain explicitly separate later physics/asset work. The next critical-path architecture item is **30 — Slang compiler and reflection metadata**, not more tuning of the current renderer-side scene/BVH dirty machinery.


### PhysX/Jolt parity hardening checkpoint — 2026-09-04

With both backends live, the generic seam was audited implementation-against-implementation and every place the two disagreed was resolved in the contract rather than left to the caller.

Corrected in the backends:

- **PhysX wrote collision filter data onto a shared `PxShape`.** `CollisionLayer` is per-`BodyDesc` in the generic API, but the filter data was applied to the template shape held by the `ShapeHandle`. A second body built from the same handle re-filtered the first, and then failed its own `attachShape` because the template was exclusive. `ShapeHandle` is now a pure description; each body instantiates its own exclusive `PxShape`, and `shapeHandles` maps instances back to the template. This matches Jolt, which always carried the layer on the body.
- **PhysX accepted body mutation while a step was in flight.** `CreateBody`, `SetBodyPose`, `SetKinematicTarget`, `AddForce`, `SetLinearVelocity`, and `SetAngularVelocity` had no `simulating` guard, so they wrote to the scene between `simulate()` and `fetchResults()` — illegal in PhysX — and returned success. All six now reject, matching Jolt.
- **Jolt could never complete a non-blocking fetch.** `JPH::PhysicsSystem::Update` is synchronous, so `FetchResults(false)` returned `false` forever and `while (!FetchResults(false))` would spin indefinitely. The step now runs in `BeginSimulation`, leaving results ready the moment it is issued; `IsSimulationInFlight()` and the write guards still describe the same `Begin -> Fetch` window.
- **`CollisionEvent::Impulse` was always zero on Jolt.** Jolt's contact listener does not hand out solver impulses, so it is now estimated with `JPH::EstimateCollisionResponse`. Measured against PhysX on the same drop, both report `18.540`.
- **Overlap results disagreed.** Jolt collapsed to one hit per body while PhysX reported one per shape. Both now de-duplicate on the `(body, shape)` pair a generic `OverlapHit` actually identifies.
- **`PhysicsMaterialDesc::StaticFriction` is documented** as honoured only by backends that separate static and dynamic friction; Jolt has one coefficient and uses `DynamicFriction`.

Performance work on the same seam:

- `PhysicsWorldDesc::EnablePersistedCollisionEvents` (default off) makes per-step persisted contact reporting opt-in. It previously cost one event per touching pair per step on both backends — plus `extractContacts` per pair on PhysX, and a global-mutex `push_back` from every worker thread on Jolt. PhysX now adds `eNOTIFY_TOUCH_PERSISTS` only when asked, through the scene's filter-shader constant block; Jolt returns from the persisted path before taking its lock.
- Jolt's scratch allocator moved from `TempAllocatorMalloc` (a general-allocator round trip per solver temporary) to a 16 MB `TempAllocatorImplWithMallocFallback`.
- Jolt now calls `OptimizeBroadPhase()` once per batch of non-moving insertions, tracked so spawning dynamic bodies never triggers it.

Contract additions holding both backends to the corrected behaviour: `RunPhysicsSharedShapeContract`, `RunPhysicsInFlightWriteContract`, and `RunPhysicsContactEventContract`.

Validation: a Windows clean build from a wiped dependency cache succeeds end to end, `SwimTests` runs 118 cases green, and the engine runs and shuts down cleanly on `--physics=physx` and `--physics=jolt` against the real Sponza scene. A cross-backend probe over the same scenario reports identical raycast, sweep, overlap, kinematic, impulse-response, collision-timing, and trigger-timing values on both backends; resting height differs by 17 mm, which is Jolt's default penetration slop.

### Physics backend file organization checkpoint — 2026-09-05

Pure code-motion cleanup (see §0.2): `JoltWorldBackend.cpp` (1355 lines) and `PhysXWorldBackend.cpp` (1150 lines) each defined every concrete filter/callback type for their backend inline in one file. No public API, contract, or gameplay-visible behavior changed; this is only where the code lives.

- **Jolt** (`Backends/Jolt/`): the six private nested helper types — `BroadPhaseLayerInterface`, `ObjectVsBroadPhaseFilter`, `ObjectPairFilter`, `QueryBodyFilter`, `QueryObjectLayerFilter`, `ContactCallback` — moved to `Filters/` and `Callbacks/`, each its own header. The anonymous-namespace math/validation helpers (`IsFiniteVec3`, `IsValidPose`, `LayersMatch`, `ToJoltMotionType`, the `BroadPhaseLayers` constants) moved to `Internal/JoltPhysicsUtils.h/.cpp` under `Engine::JoltPhysicsDetail`, so the filter/callback files and `JoltWorldBackend.cpp` share one definition instead of each carrying a private copy. `JoltWorldBackend.cpp` itself shrank to ~1069 lines and now holds only `JoltWorldBackend`'s own constructor/destructor and member functions.
- **PhysX** (`Backends/PhysX/`): `LayerQueryFilter` and `PhysXWorldBackend::SimulationEventCallback` moved to `Filters/` and `Callbacks/`. The equivalent helpers plus the `PxSimulationFilterShader` function moved to `Internal/PhysXPhysicsUtils.h/.cpp` under `Engine::PhysXPhysicsDetail` — a separate namespace from Jolt's, on purpose: PhysX and Jolt both had `IsFiniteVec3`/`IsFiniteQuat`/`IsValidPose`/`IsValidDirection` helpers with identical signatures, and promoting both into one shared namespace would have collided at link time. `PhysXWorldBackend.cpp` shrank to ~886 lines.
- **Bug found while splitting, not introduced by it:** `JoltWorldBackend.h` declares the `ToGlm(JPH::RVec3Arg)` overload only under `#ifdef JPH_DOUBLE_PRECISION`, but `JoltWorldBackend.cpp` defined it unconditionally. `cmake/JoltDependencies.cmake` builds Jolt with `DOUBLE_PRECISION OFF`, under which Jolt's own `Real.h` aliases `RVec3Arg` to `Vec3Arg` — so the unguarded second definition was a duplicate definition of the same overload the header already declares. Wrapped the `.cpp` definition in the same `#ifdef` the header uses; behavior is unchanged (that overload was never reachable in this project's build configuration either way), the code now simply compiles.
- Already-small, already-single-purpose files (`JoltBackend`/`JoltBackendFactory`, `PhysXBackend`/`PhysXBackendFactory`, the top-level `Physics/*.h/.cpp`) were left untouched — they never had the one-file-many-classes problem this checkpoint addresses.
- No genuinely deprecated/dead code was found in `Source/Engine/Systems/Physics/` to relocate into `Deprecated/`.

Validation: every new/changed file, plus each `.cpp` that transitively includes them, was checked with `g++ -std=c++20 -fsyntax-only` against the real CPM-pinned Jolt v5.6.0 / PhysX headers with this project's actual build flags (`DOUBLE_PRECISION OFF` for Jolt, `PX_PHYSX_STATIC_LIB` for PhysX). A line-range coverage check against each extraction script's own `extract()` calls confirmed no code was dropped or duplicated (every gap between extracted ranges is blank-line-sized only). This execution environment cannot run the real MSVC/Windows build; run `scripts\build-windows-soft.bat` to confirm on the actual toolchain.

### Phase 6 exit criteria

- [ ] same `PhysicsSandbox` runs with `--physics=physx` and `--physics=jolt`.
- [x] gameplay/scene generic headers contain no PhysX/Jolt implementation types. *(Verifier-enforced; backend implementation types are confined to backend directories/targets.)*
- [x] backend switching requires no gameplay recompilation logic or `if constexpr` branching. *(The compiled backend factories are selected from `EngineConfig::Physics`; gameplay and scene code remain on the generic contracts.)*

---

## Phase 7 — Slang shader system and reflection contracts

Do this before final RHI descriptor/pipeline architecture is locked down.

### Source policy

- [x] all first-party shader source uses `.slang`; no handwritten `.hlsl` or `.glsl` remains under `Source/Shaders`. The pre-Slang sources are archived at `Deprecated/Shaders/` — outside `Source/`, so no CMake glob can reach them — and the build-layout verifier fails if either a retired source reappears under `Source/Shaders` or the build system references the archive.
- [x] the old DXC/HLSL first-party compilation path is retired; Slang owns Vulkan shader compilation. The build no longer requires `dxc.exe`.
- [x] Vulkan runtime output is Slang-generated SPIR-V.
- [x] OpenGL legacy consumes Slang-generated GLSL compatibility artifacts while it remains supported; handwritten GLSL source is retired.
- [ ] D3D12 future output is DXIL;
- [ ] Metal remains a future target and must not be assumed complete until toolchain support is production-ready.

### Shader module structure

Prefer composable modules over large preprocessor permutation forests.

Example concepts:

```text
Shaders/
  Common/
  Material/
  Lighting/
  Visibility/
  Shadows/
  Post/
  Ui/
  Debug/
```

### Reflection

The compiler pipeline should emit/reflection-cache:

- entry points/stages;
- parameter blocks;
- resource bindings;
- push/root constants;
- specialization constants;
- vertex inputs where relevant;
- thread-group sizes;
- parameter offsets/types;
- feature/variant metadata.

Use this to validate or generate matching C++ schemas.

### Variant policy

- [ ] variants are declared by material/shader templates;
- [ ] use Slang interfaces/generics where they reduce preprocessor complexity;
- [ ] do not compile every theoretical boolean combination;
- [ ] hash source include graph + compiler version + options + features;
- [ ] compile cache by content hash;
- [ ] shipping builds do not compile shaders during frames;
- [ ] dev hot reload compiles asynchronously and swaps pipelines after safe retirement.

### RHI-facing shader artifact

High-level renderer should consume a backend-independent shader program description plus compiled target blob/reflection metadata.

Do not make render passes know `.spv` paths.


### Slang/RHI/Vulkan-bootstrap checkpoint — 2026-09-04

Critical-path items 30–38 are now implemented without widening the legacy renderer or scene/BVH surface:

- `cmake/ShaderCompilerDependencies.cmake` pins the official Slang `2026.16.1` compiler SDK by platform and verifies the release archive SHA-256 before extraction. Slang is a build-only compiler dependency; the engine does not link the Slang runtime/library merely to consume shader artifacts.
- `cmake/SlangShaders.cmake` provides deterministic CMake `OUTPUT` rules for backend shader artifacts plus reflection JSON and consumes Slang's depfile for source/include dependency tracking. It deliberately does not use `PRE_BUILD`. `cmake/Shaders.cmake` now sends every first-party Vulkan shader through Slang -> SPIR-V and every isolated legacy OpenGL shader through Slang -> generated GLSL; the old DXC/HLSL and handwritten-GLSL source paths are retired.
- `Swim::ShaderCompiler` owns a stable first-party reflection schema. `simdjson` is private to its parser implementation, and no Slang/simdjson type appears in the public metadata header.
- `Source/Shaders/Slang/Basic/Basic.slang` remains the reflection-first reference module and uses shader attributes plus `ParameterBlock` declarations. The former Vulkan HLSL and OpenGL GLSL shader sets have also been ported to `.slang`, preserving their current runtime artifact names so shader-language migration does not force unrelated renderer surgery.
- `RHI/RhiTypes.h` establishes backend-neutral formats, buffer/texture usage, resource states, descriptor schema vocabulary, shader-stage masks, and a capability table. Its public-header gate is intentionally dependency-free and rejects Vulkan/D3D12/Metal implementation types.
- The backend-neutral `Swim::Rhi` interface target now also defines the item 33 object/lifetime contract: `GraphicsSystem`, `Adapter`, `Device`, queue/swapchain/command objects, buffers/textures/views/samplers, shader and graphics/compute pipeline objects, descriptor tables, fences/timelines, and query pools. Swapchain creation accepts a forward-declared `Platform::Window&`; native handles are explicit escape hatches rather than the normal API.
- Descriptor and pipeline layout ownership has been tightened before any Vulkan implementation is written: `ShaderProgram` owns the reflected descriptor/push-constant interface, `PipelineLayoutDesc` references a `ShaderProgram` instead of accepting a second hand-written schema, descriptor tables reference a reflected pipeline-layout space, and individual descriptor writes no longer repeat descriptor type information. The backend must validate writes against reflected program metadata.
- Item 34 is implemented as the backend-neutral `GraphicsFactory`: backend creation functions register explicitly by `GraphicsApi`; duplicate/invalid registration is rejected; factory creation has no Vulkan/Win32 dependency or global static preregistration.
- Item 35 is implemented as `Swim::RhiVulkan`: volk `1.4.350` and vk-bootstrap `v1.4.350` are pinned privately; volk is namespaced and uses per-instance/per-device dispatch tables; vk-bootstrap enumerates every suitable Vulkan 1.3 adapter and builds the logical device plus graphics/compute/transfer queues. Swim requires dynamic rendering, synchronization2, timeline semaphores, descriptor indexing/runtime arrays, indirect count, and buffer device address at selection time; adapter capability reporting is populated from Vulkan feature/property/memory queries. The device enables `VK_KHR_swapchain` up front so item 36 does not require device recreation.
- Item 36 is implemented without leaking SDL or OS-native handles into the RHI: `Swim::Platform` owns an internal opaque Vulkan-WSI bridge that acquires/releases SDL's Vulkan loader, exposes SDL's required instance-extension list, filters queue families through SDL presentation support, and creates/destroys surfaces for `Platform::Window`. `Swim::RhiVulkan` consumes that bridge, revalidates the selected graphics queue against each concrete surface, and owns a baseline SDR swapchain path with image/image-view wrappers, acquire/present, configurable FIFO vs mailbox/immediate fallback presentation, resize/recreate, and the binary semaphore/fence primitives required by WSI. The original item-36 implementation used a temporary `vkDeviceWaitIdle` resize stopgap; item 38 has now removed that device-wide wait. Minimized-window and HDR capability hardening are implemented in the later Phase 9 checkpoints below; desktop validation remains open.
- Item 37 is implemented behind the RHI resource contracts with VMA `v3.4.0`, pinned privately under `Swim::RhiVulkan`. The allocator uses the same SDL/volk-resolved Vulkan function path as the backend, enables buffer-device-address support and memory-budget integration when available, and is destroyed before the logical device. `Device::CreateBuffer` now maps `DeviceLocal`, `CpuToGpu`, and `GpuToCpu` to VMA automatic memory policy; `Device::CreateTexture` owns device-local images; `CreateTextureView` owns non-swapchain views while swapchain views remain swapchain-owned. Normal buffer/image destruction is paired with VMA allocation destruction, and allocation debug names are retained without exposing VMA in public RHI headers.
- Item 38 establishes frame lifetime before the render smoke path expands command recording: backend-neutral `RhiFrameLifetime.h` owns a configurable ring of frame contexts, one command pool per context, frame-owned command lists, a private monotonically increasing completion timeline, and deferred `RhiObject` ownership. Context reuse waits only that context's completion value; ordinary retirement never calls device/queue idle, and invalid explicit retirement points are rejected before resource ownership transfers. The Vulkan backend implements timeline semaphore create/query/wait, `vkQueueSubmit2` synchronization2 submission for binary/timeline waits/signals, same-device validation, and command-pool/list allocation/reset/begin/end. Queue wrappers sharing the same native `VkQueue` also share an external-synchronization mutex.
- Swapchain resize now requires an explicit `TimelinePoint` proving the last render work that referenced the old images is complete. After that timeline point, the Vulkan WSI path waits only the presentation queue before destroying the old swapchain because core Vulkan does not expose presentation-engine completion through the render timeline. This replaces the former `vkDeviceWaitIdle` stopgap while remaining correct on drivers without optional swapchain-maintenance present fences.
- Dependency-enabled builds also compile/link a Vulkan-backend public-header gate and a registration-only `GraphicsFactory` test without requiring a GPU/window. The offline/foundation build is green after item 38 at **61 cases / 287 checks** plus the ShaderCompiler and generic RHI public-header compile gates. This environment still cannot fetch/build the real Vulkan bootstrap/VMA dependencies or execute `slangc`, so those dependency-enabled compile/runtime gates must run in the normal clean build rather than being claimed here.

**Next implementation checkpoint:** item 39, build the validation-clean RHI clear/triangle/texture smoke path on Windows and Linux. That checkpoint should finish the currently stubbed Vulkan command recording/pipeline/descriptor pieces needed by the smoke test rather than starting RenderGraph early.

### Phase 7 Slang/RHI bring-up checkpoint — 2026-09-04

The Slang pipeline, the shader-reflection tool boundary, the backend-neutral RHI contracts, and the Vulkan RHI backend now configure, build, and run. Issues found and fixed while validating the bring-up:

- **GLSL targets produced no output.** `slangc` emits one kernel per entry point for GLSL, so `-o` must follow an `-entry`. Every OpenGL shader failed with `E00070`. The runtime shader rules now pass `ENTRY_POINT main` for GLSL programs; SPIR-V still compiles the whole module.
- **The platform WSI shim did not compile.** `VulkanWsi.cpp` used `VK_NULL_HANDLE`, which `SDL3/SDL_vulkan.h` does not define — it forward-declares the handle types only. Pulling in `vulkan.h` would have put the Vulkan API back inside `Swim::Platform`, so the surface handle is value-initialized instead.
- **The modern RHI could not build against an older Vulkan SDK.** volk and vk-bootstrap 1.4.350 reference Vulkan 1.4 feature structures; a developer on the 1.3 SDK failed with dozens of undeclared-identifier errors inside vk-bootstrap. `cmake/VulkanRhiDependencies.cmake` now pins `Vulkan-Headers` v1.4.350 and points both dependencies at it. The legacy renderer keeps using the installed SDK; the two never exchange Vulkan types because the RHI boundary is opaque handles and the WSI shim resolves everything through SDL.
- **One ported shader was mis-translated.** `decorator_fragment.slang` contained `float2(0.0f)x` where the GLSL original had `vec3(0.0)`, which failed to parse.
- **Every SPIR-V module was a validation error.** `-fspv-reflect` embeds `SPV_GOOGLE_user_type` / `SPV_GOOGLE_hlsl_functionality1` decorations, which require the matching `VK_GOOGLE_*` device extensions. Swim reads reflection from the `-reflection-json` sidecar — byte-identical with or without the flag — so the flag was removed.
- **Bindless indexing needed a device feature the renderer never enabled.** Slang emits the `SampledImageArrayNonUniformIndexing` capability for the unsized `Texture2D textures[]` binding where DXC did not. `shaderSampledImageArrayNonUniformIndexing` is separate from `descriptorIndexing`, so `VulkanDeviceManager` now requires and enables it. The shader source itself is byte-identical to the retired HLSL apart from the `[shader("fragment")]` attribute.
- **Slang changed the meaning of `SV_InstanceID` relative to the retired DXC SPIR-V path.** The legacy renderer intentionally packs many mesh buckets into one instance SSBO and uses each indirect command's non-zero `firstInstance` as the global SSBO/visible-index base. DXC's old path exposed raw Vulkan `InstanceIndex` here, while Slang's `SV_InstanceID` is BaseInstance-relative. That caused later mesh buckets to read element zero again, producing cross-mesh transforms/materials (for example barrel geometry at cube transforms and one texture repeated across Sponza). Every shader that consumes the global instance base now explicitly uses `SV_VulkanInstanceID`; integer material/instance varyings are flat/nointerpolation and per-instance bindless texture indices use `NonUniformResourceIndex`. The build-layout verifier locks that semantic contract.
- **Shader/C++ memory layout was audited instead of guessed at.** Runtime Slang compilation explicitly retains `-matrix-layout-column-major` to preserve the retired DXC matrix-memory convention, and shader-shared legacy instance/BVH/decorator/MSDF structs now have host-side `sizeof`/`offsetof` assertions. This keeps the transitional renderer computationally defined while those data paths are later replaced by the modern RHI/GPU-scene work.
- **Depfiles shipped beside the executable.** `swim_add_slang_program` wrote `<name>.d` into the artifact directory, which is deployed wholesale. Depfiles now go to a separate `Generated/ShaderDeps` tree, so only `.spv`, `.glsl`, and `.reflection.json` reach the runtime directory.
- **One RHI test used a nonexistent macro** (`SWIM_TEST_CASE`) and slash-separated suite naming, so it did not compile and would not have been addressable by the runner's dotted filters.

Validation: configure and build are green, `SwimTests` runs 129 cases across 36 suites including the new `RHI.*`, `RHI.Vulkan`, and `ShaderCompiler.*` suites, all 22 first-party shaders compile through the pinned `slangc`, and the engine runs and shuts down cleanly on Vulkan with **zero validation-layer errors** using only Slang-generated SPIR-V.

The later visual-corruption report is an important reminder that validation-clean SPIR-V is not semantic parity. The source-level audit against `Deprecated/Shaders/Vulkan` found the BaseInstance-relative `SV_InstanceID` migration mismatch described above; the correction is guarded structurally here, but the post-fix visual runtime still needs to be exercised by the normal dependency-enabled Windows build.

Known unrelated issue: the legacy OpenGL renderer fails at startup in `SetPixelFormatForHDC` ("SetPixelFormat failed for multisample format"). This was confirmed pre-existing by building and running the committed baseline, which fails identically. It happens during WGL pixel-format selection, before any shader is loaded, and `SwimEngine` does not link the RHI targets, so it is independent of this work.

### Visual Studio parallel-configure checkpoint — 2026-09-04

A Release build from inside Visual Studio failed with `LNK1181: cannot open input file 'Release\SwimAssets.lib'`, `could not lock config file .git/config: File exists`, and an `MSB8066` cascade across most module targets, while the Ninja soft build of the same tree was green.

Root cause: CMake's Visual Studio generator attaches a `--check-stamp-file` custom build to **every** generated project. MSBuild builds projects in parallel, so whenever the generate stamp is stale — which it is after any CMake edit — one full CMake configure is launched *per project*, concurrently, against a single binary directory. Those configures overwrite each other's generated files (observed as `configure_file` failures from simdjson and Draco) and race on the shared CPM dependency caches. `cmake/PhysX.cmake` normalizes the pinned PhysX checkout at configure time with `git config --local`, `git reset --hard`, and `git clean -ffdx`, so the collision surfaced as a Git lock error. The configure that lost the race failed, its target's custom build exited 1, and every target depending on it — including `SwimAssets` — never produced a library, which is what the linker then reported.

Fixed in two places:

- `CMAKE_SUPPRESS_REGENERATION` is now forced on for the Visual Studio generator as well as Ninja, so no project regenerates the build system and the IDE can never start a configure storm. This matches the contract the repository already relies on: every supported workflow configures explicitly before compiling, and both build scripts refresh the solution on every run. The trade is that CMake edits require re-running a build script or `cmake --preset windows-vs` before building in the IDE, which is now documented in `README.md` and `docs/VisualStudioProjectStructure.md`.
- `cmake/PhysX.cmake` no longer writes to the shared dependency cache unconditionally. It reads the current Git config values and working-tree status first and only normalizes/resets when the checkout has actually drifted. This removes the write from the common path entirely (defence in depth for any other concurrent configure) and also stops every configure from paying a full `reset --hard` plus `clean -ffdx` over the PhysX tree.

`verify-build-layout.py` now requires both invariants: the suppression guard covering both generators, and the read-before-write shape of the PhysX normalization.

Validation: `msbuild SwimEngine.sln /p:Configuration=Release /m` completes with zero errors, both from a hand-run configure and from a solution freshly regenerated by the soft-build script. The Visual Studio-built `SwimTests.exe` runs 129 cases green and the Visual Studio-built engine runs and exits cleanly on Vulkan with zero validation-layer errors. The Ninja soft build remains green.

### Phase 7 exit criteria

- [x] one Slang shader compiles to Vulkan SPIR-V and reflects its bindings. *(The dependency-enabled bring-up compiles all 22 first-party Slang programs and emits reflection sidecars.)*
- [x] the same source can produce a legacy OpenGL-consumable artifact for a validation sample where practical. *(The Slang runtime rules emit the isolated legacy GLSL artifacts from `.slang` sources.)*
- [x] C++/shader parameter layout validation exists. *(The transitional Camera/instance/BVH/decorator/MSDF host structs have explicit size/offset/alignment guards, and runtime Slang compilation locks the DXC-compatible matrix memory convention; later reflection-driven schema generation can replace these manual ABI assertions.)*
- [x] descriptor/pipeline layout design no longer duplicates binding definitions manually in multiple source files. *(`ShaderProgram` owns the reflected interface; pipeline layouts and descriptor tables derive from it.)*

---

# Part III — Modern graphics architecture

## Phase 8 — RHI contracts and runtime graphics backend factory

The RHI is an explicit GPU API abstraction. It is not the high-level renderer.

### Backend selection without `constexpr`

Create a factory at startup:

```cpp
std::unique_ptr<Rhi::GraphicsSystem> CreateGraphicsSystem(
    GraphicsBackend Backend,
    Platform::Window& PresentationWindow,
    const GraphicsConfig& Config);
```

After creation, high-level renderer code talks to RHI interfaces/dispatch rather than branching on backend enums.

### Avoid two bad extremes

Do not:

1. expose Vulkan everywhere and call a thin renderer wrapper an RHI;
2. design a lowest-common-denominator API around OpenGL.

The RHI should model modern explicit APIs naturally.

### Core RHI concepts

- [x] `GraphicsSystem`
- [x] `Adapter`
- [x] `Device`
- [x] `Queue`
- [x] `Swapchain`
- [x] `CommandPool` / `CommandAllocator`
- [x] `CommandList`
- [x] `Buffer`
- [x] `Texture`
- [x] `TextureView`
- [x] `Sampler`
- [x] `ShaderProgram`
- [x] `PipelineLayout`
- [x] `GraphicsPipeline`
- [x] `ComputePipeline`
- [x] `DescriptorSchema` / resource layout
- [x] `DescriptorTable` / bind group/resource table
- [x] `Fence`
- [x] `Timeline`
- [x] `QueryPool`

### Resource descriptions

Define backend-neutral enums/structs for:

- format;
- dimensions;
- usage;
- memory preference;
- resource states;
- load/store ops;
- blend;
- compare;
- stencil;
- cull/front-face;
- primitive topology;
- sample count;
- copy regions;
- viewport/scissor;
- attachment descriptions.

### Capabilities

The renderer queries capabilities rather than asking what API it is running on.

Examples:

- descriptor indexing/bindless limits;
- buffer device address;
- indirect count;
- subgroup features;
- mesh/task shaders;
- descriptor buffer;
- async compute/dedicated transfer queues;
- timestamp support;
- ray query/ray tracing;
- sparse residency;
- compressed texture formats;
- HDR swapchain/color spaces;
- memory budget reporting.

### Runtime polymorphism strategy

Runtime backend selection does **not** mean adding a backend `switch` inside every mesh draw.

Preferred options:

- coarse-grained virtual interfaces at system/device boundaries;
- backend dispatch/function table initialized once;
- opaque move-only RHI objects/handles;
- command recording APIs that dispatch cheaply through the selected device implementation.

The exact mechanism can be benchmarked, but architectural cleanliness matters more than saving a theoretical nanosecond around GPU API calls.

### Native escape hatches

Advanced users may request backend-native handles explicitly:

```text
GetNativeDeviceHandle()
GetNativeBufferHandle()
GetNativeTextureHandle()
```

These APIs are clearly unsafe/backend-specific and never the normal renderer path.

### OpenGL policy

OpenGL is not required to implement this modern RHI.

Use:

```text
GraphicsBackend::Vulkan
    -> RHI -> Modern Renderer

GraphicsBackend::D3D12 future
    -> RHI -> Modern Renderer

GraphicsBackend::Metal future
    -> RHI -> Modern Renderer

GraphicsBackend::OpenGLLegacy
    -> Legacy OpenGL Renderer
```

This preserves working OpenGL without poisoning the explicit RHI with GL-era limitations.

### Phase 8 exit criteria

- [x] no Vulkan type exists in generic render/RHI public headers.
- [x] RHI accepts the platform window abstraction, not `HWND`.
- [x] graphics backend is selected at runtime.
- [ ] a null/mock RHI can support unit tests where useful.
- [ ] D3D12/Metal can plausibly implement the contract without redesigning it.

---

## Phase 9 — Vulkan 1.3 backend

Vulkan is the first production implementation and should be excellent rather than genericized downward.

### Bootstrap stack

Use:

- volk for dispatch;
- vk-bootstrap for instance/device/adapter/queue bootstrap where it removes boilerplate;
- SDL3 Vulkan WSI for surface creation;
- VMA for memory allocation/suballocation/budget tracking.

Swim still owns:

- required feature policy;
- preferred adapter policy;
- queue strategy;
- capability table;
- debug naming;
- lifetime rules;
- render-graph integration.

### Vulkan baseline features

Prefer Vulkan 1.3 baseline on supported desktop hardware:

- [x] dynamic rendering;
- [x] synchronization2;
- [x] timeline semaphore feature support; *(item 38 now owns the frame/timeline lifetime model as well as Vulkan timeline create/query/wait/submit)*
- [x] descriptor indexing;
- [x] indirect count;
- [x] buffer device address where beneficial;
- [x] pipeline cache; *(shared native cache, validated import/export and cold/warm persistence smoke; native desktop validation remains pending)*
- [x] debug utils; *(optional instance extension, captured messages, object names and command labels; see diagnostics checkpoint)*
- [x] memory-budget telemetry. *(owned per-heap driver estimates and allocator fallback; native desktop validation remains pending)*

Optional/capability-gated:

- descriptor buffer;
- mesh/task shader path;
- ray query/ray tracing;
- variable-rate shading;
- sparse resources.

### Resource allocation

Replace raw general `vkAllocateMemory` usage with VMA.

Create:

- [x] device-local allocation policy; *(normal RHI buffers/images now use VMA automatic device/host preference policy)*
- [x] persistent mapped upload arenas/rings; *(bounded per-context arenas, persistent Vulkan mapping, automatic flush and timeline-safe reuse; native desktop validation remains pending)*
- [x] readback arenas; *(bounded persistent mapping, frame-submission completion, explicit CPU-result lifetime and non-coherent invalidation; native desktop validation remains pending)*
- [ ] transient frame buffers via render graph;
- [x] memory-budget reporting; *(renderer-facing per-heap snapshots expose VMA counters, fresh driver estimates, explicit fallback and safe headroom)*
- [x] allocation debug names/tags. *(RHI resource debug names are copied into owned storage and assigned to VMA allocations.)*

### Frame synchronization

Use timeline-driven retirement for engine resources.

```text
FrameContext[N]
  |-- command allocators
  |-- transient CPU arena
  |-- transient descriptors
  |-- upload-ring slice
  |-- query slice
  |-- deferred destruction list
  `-- completion timeline value
```

Rules:

- no routine `vkDeviceWaitIdle`;
- no queue idle in ordinary upload helpers;
- no object destruction until the relevant timeline value completed;
- bindless indices retire on the GPU timeline before reuse.

### Swapchain

- [x] resize/recreate without normal device-idle; *(replacement waits the caller-provided frame timeline and uses a presentation-queue-only WSI fallback before old swapchain destruction; no device-wide idle remains in resize)*
- [x] minimized/zero-size window handling; *(dormant creation, acquisition suspension, explicit resume/rebuild; desktop validation remains pending)*
- [x] SDR baseline;
- [x] HDR capability path; *(per-window format/color-space query, explicit SDR/prefer-HDR/require-HDR policy, PQ/scRGB negotiation and resize revalidation; native HDR desktop validation remains pending)*
- [x] present mode configuration;
- [x] multiple swapchains/windows supported at RHI object/lifetime level.

### Validation and diagnostics

- [x] validation requested in Debug builds; *(explicit Required mode is used by every opt-in smoke in every build configuration)*
- [x] GPU-assisted and synchronization-validation configuration; *(explicit runtime checks via modern layer settings, strict capability requirements, configured-state reporting and selectable desktop smoke profiles; native execution remains pending)*
- [x] Vulkan object names; *(implemented descriptor names and fixed infrastructure names via debug utils)*
- [x] command labels; *(nested regions and point markers, with recording/balance validation)*
- [x] adapter/driver info logging; *(device/API/driver strings and vendor/device/raw-driver identifiers)*
- [x] device-loss diagnostics; *(typed sticky reports, optional bounded native fault details and lost-device retirement; native GPU fault validation remains pending)*
- [x] RenderDoc-friendly markers; *(debug-utils annotations implemented; actual GPU-tool capture remains unverified)*
- [x] GPU timestamps. *(queue-specific precision, GPU reset, begin/end writes, nonblocking availability readback and wrap-safe elapsed time; native desktop validation remains pending)*

### Vulkan RHI file organization checkpoint — 2026-09-05

Pure code-motion cleanup (see §0.2): `VulkanRhiBackend.cpp` had grown to 2353 lines and defined nearly every concrete `Swim::RhiVulkan` type — device, adapter, swapchain, queue, command pool/list, every resource and sync primitive — in one anonymous namespace in one file. No `Swim::Rhi` contract, public header, or runtime behavior changed; this is only where the code lives.

- Split one type per file under `Source/Engine/Systems/Renderer/RHI/Backends/Vulkan/`: `Commands/` (`VulkanCommandPool`, `VulkanCommandList`, `VulkanCommandPoolState`), `Resources/` (`VulkanBuffer`, `VulkanTexture`, `VulkanTextureView`), `Sync/` (`VulkanSemaphore`, `VulkanFence`, `VulkanTimeline`), and `Internal/` (shared device/instance bootstrap state, native-handle conversion templates, format/usage-flag conversion helpers, queue-family selection) — each promoted from the original anonymous namespace to the normal `Swim::RhiVulkan` namespace, since types split across translation units need external linkage to be visible to each other. `VulkanDevice`, `VulkanAdapter`, `VulkanGraphicsSystem`, and `VulkanQueue` (declaration in `.h`, `Submit`/`WaitIdle` bodies in `.cpp`, matching the file's own pre-existing style) sit at the top level. `VulkanRhiBackend.cpp` itself shrank to 153 lines: instance bootstrap plus the `CreateGraphicsSystem`/`RegisterGraphicsBackend` factory entry points.
- **Follow-up split, same day:** `VulkanSwapchain` was still a single ~354-line header with every method (constructor through `Rebuild`/`DestroySwapchain`) defined inline in the class body. Its trivial one-line accessors (`GetNativeHandle`, `GetFormat`, `GetExtent`, `GetImageCount`, `GetImageView`) and the constructor stayed inline; `~VulkanSwapchain`, `Initialize`, `AcquireNextImage`, `Present`, `Resize`, and the private `Rebuild`/`DestroySwapchain` moved to `VulkanSwapchain.cpp` as out-of-line `VulkanSwapchain::` definitions, with their bodies unchanged. The header is now 94 lines of declarations; the `.cpp` is 292 lines.
- Four line-range transcription mistakes in the original extraction (an off-by-one dropping a struct's closing brace, a missing function signature line) were found and fixed the same way: verified against the real Vulkan-Headers/volk/vk-bootstrap/VMA versions this project pins, not guessed at.
- No genuinely deprecated/dead code was found in `Source/Engine/Systems/Renderer/RHI/` to relocate into `Deprecated/`.

Validation: `VulkanRhiBackend.cpp`, `VulkanQueue.cpp`, `VulkanSwapchain.cpp`, `Internal/VulkanFormatUtils.cpp`, `Internal/VulkanQueueFamilies.cpp`, and the `RHI.Vulkan` public-header gate all pass `g++ -std=c++20 -fsyntax-only` against the real CPM-pinned Vulkan-Headers/volk/vk-bootstrap/VMA versions with this project's actual macros (`VOLK_NAMESPACE`, the VMA function-table/version defines). A header-inclusion trace confirmed every new file is actually reached from the main backend compile, and a line-range coverage check against the extraction script's own `extract()` calls confirmed no code was dropped or duplicated. This execution environment cannot run the real MSVC/Windows build; run `scripts\build-windows-soft.bat` to confirm on the actual toolchain.

### RHI clear and transfer implementation checkpoint — 2026-09-05

This is the first bounded implementation checkpoint inside critical-path item **39**, not closure of the clear/triangle/texture validation gate. The legacy renderer and physics backend behavior are unchanged.

- [x] Implement synchronization2 buffer/image barriers with explicit source/destination state, access masks, layouts, and validated mip/layer ranges. Whole remaining ranges are resolved before dispatch; combined depth/stencil formats transition both aspects. Same-state write barriers are retained. Unknown/incompatible states and missing creation usage are rejected.
- [x] Add backend-neutral `Buffer::Write` / `Buffer::Read`, `HostWrite` / `HostRead`, and `BufferTextureCopyRegion`. CPU access uses VMA's mapping/copy/cache-maintenance helpers and does **not** hide a GPU wait. Callers finish upload writes before submitting; readback requires a transfer-to-host barrier and completion wait before `Read`.
- [x] Implement validated buffer copies, matching-format image copies, and tightly packed buffer-to-image / image-to-buffer copies. Bounds checks use subtraction and checked multiplication to reject overflow; overlapping buffer ranges, foreign-device resources, wrong usages, invalid mips/layers, and misaligned buffer/image offsets fail before command dispatch.
- [x] Implement dynamic-rendering color and depth/stencil load/clear/store, rendering-scope checks, viewport, and scissor recording. The Vulkan viewport uses negative height to preserve the canonical +Y-up NDC convention; generic camera matrices are not modified. Attachment extent/sample compatibility and device framebuffer/viewport limits are checked.
- [x] Keep command implementation modular: `VulkanCommandList.h` declares the type; lifecycle, transfers, and rendering live in `VulkanCommandList.cpp`, `VulkanCommandListTransfers.cpp`, and `VulkanCommandListRendering.cpp`. Shared state/transfer/resource-validation helpers live under `Internal/`; `VulkanBuffer` CPU access lives in its own `.cpp`.
- [x] Tighten the one-time command-buffer lifecycle. Pool generations invalidate old recordings, rerecording requires a pool reset, and queue submission rejects unfinished, duplicate, or already-submitted command lists. Failed native submission preserves the executable state for retry. GPU completion before pool reset/destruction remains the caller/frame ring's responsibility.
- [x] Reject cross-device texture views and non-array views spanning multiple array layers in `CreateTextureView`.
- [x] Add ten automatically registered `SwimTests` cases covering native dispatch capture and invalid-input rejection, with no GPU/window requirement. Vulkan implementation dependencies are private to the test executable; the generic RHI and public factory header remain Vulkan-free.
- [x] Add an opt-in `RHI.Vulkan.Smoke.ClearTransferAndPresent` case in the same test executable. It reads back an RGBA8 clear, checks a patterned buffer -> image -> image -> buffer round trip and a buffer copy, then acquires/clears/presents six swapchain frames. Acquire semaphores belong to frame slots, presentation-ready semaphores belong to swapchain images, and shutdown waits the presentation queue after draining the render timeline. Enabling the case makes missing desktop/GPU support a **failure**, not a silently successful test.
- [x] Repair validation tooling after the preceding organization pass: `verify-build-layout.py` now examines the recursive active backend trees for the existing physics/Vulkan requirements and reads `VulkanSwapchain.cpp` directly for the no-device-idle/WSI fallback checks. Jolt's forbidden-PhysX scan also includes subfolders. A real GCC build additionally exposed and fixed the missing `<algorithm>` include in the extracted `VulkanFormatUtils.cpp`.
- [x] Add shader modules, empty reflected pipeline layouts, graphics pipelines, and direct/indexed draw recording for the Slang triangle smoke. See the triangle checkpoint below.
- [x] Add fixed-count descriptor-bearing reflected layouts, descriptor tables/writes, samplers, and sampled 2D texture drawing. See the texture checkpoint below for supported types and binding/lifetime rules.
- [x] Run clear/triangle/texture with clean Vulkan validation diagnostics on Windows and Linux, including resize/minimize/restore coverage, before closing item 39 or starting RenderGraph. *(Done: all nineteen item 39 smokes pass the four validation profiles on Windows and Linux; see [the item 39 record](validation/Item39-2026-09-12.md).)*

**Explicit baseline limits:** barriers and image/rendering work currently require the graphics queue family. This does not implement queue-family ownership transfers or dedicated-transfer image granularity policy. `CopyBuffer` can record on a transfer family, but ownership/synchronization is still explicit. Image copies are matching-dimension, matching-format uncompressed color copies between distinct resources; buffer/image copies additionally require single sampling and tightly packed rows. Compressed/depth-stencil transfers, integer color clears, independent stencil load/store, and read-only depth rendering attachments need later contract extensions; unsupported paths reject instead of silently choosing a representation. Resource states remain caller-supplied until RenderGraph tracking exists. Persistent upload/readback arenas and async residency are still items 41–44.

**Validation in this environment:** a dependency-enabled GCC 13/C++20 Debug foundation build compiles and links the real pinned Vulkan-Headers/volk/vk-bootstrap/VMA backend and both RHI public-header gates. `SwimTests` passes **85 cases / 551 checks**, including **26 RHI cases / 167 checks**. The build-layout verifier passes. This build used the real SDL/GLM/mimalloc/enkiTS dependencies with asset/shader compilers, Jolt, PhysX, and the legacy Windows engine excluded; it is not a full Windows engine build. The real-driver smoke was attempted and stops at SDL initialization with `No available video device`, before any GPU validation. Windows/MSVC and actual GPU pixel/presentation validation therefore remain pending.

To run the current clear/transfer and triangle smoke on a desktop with the full required Vulkan feature baseline, configure with `SWIM_BUILD_SHADER_COMPILER=ON`, build the Debug `SwimTests` target and enable the opt-in cases. The normal Windows/Linux presets already enable the shader compiler. Run with the Vulkan validation layers installed; the smoke now requires active validation and fails on captured warnings/errors or dropped messages after teardown, as well as checking pixels. This includes reflected sampled-texture readback, table replacement, and the window-lifecycle smoke described below. Desktop execution and clean validation diagnostics remain required evidence.

```powershell
$env:SWIM_RUN_RHI_SMOKE = "1"
.\build\windows-debug\SwimTests.exe --filter=RHI.Vulkan.Smoke
Remove-Item Env:SWIM_RUN_RHI_SMOKE
```

```bash
SWIM_RUN_RHI_SMOKE=1 ./build/linux-debug/SwimTests --filter=RHI.Vulkan.Smoke
```

Implementation references: [Khronos synchronization examples](https://docs.vulkan.org/guide/latest/synchronization_examples.html) and [Khronos dynamic rendering sample](https://docs.vulkan.org/samples/latest/samples/extensions/dynamic_rendering/README.html).

### RHI graphics triangle implementation checkpoint — 2026-09-05

This checkpoint supplies the first graphics pipeline and draw path inside item **39**. It does not close the cross-platform GPU gate or switch gameplay to the new renderer.

- [x] Implement Vulkan shader programs from compiler-produced SPIR-V. Byte spans are copied to aligned words; entry-point names and interface metadata are owned by the program. Unsupported/duplicate stages, malformed headers, invalid byte lengths, and invalid names are rejected. Partially created native modules are released on failure. The caller still supplies valid compiled code and its matching reflection; this factory is not a SPIR-V validator.
- [x] Create pipeline layouts from program-owned reflection. The initial triangle path required an empty descriptor/push-constant interface. The texture checkpoint below adds fixed-count descriptor layouts; the 2026-09-07 push-constant checkpoint adds reflected ranges and recording. The program must outlive its layout because `GetProgram()` returns a reference.
- [x] Create dynamic-rendering graphics pipelines with vertex/fragment stages, topology, culling/winding, depth/stencil state, color blending/write masks, sample count, and dynamic viewport/scissor. Attachment formats/features and device sample limits are checked. Wireframe, depth clamp, and differing per-attachment blend states are rejected because those optional device features are not enabled. Pipelines own their attachment signatures and remain valid after creation-time shader modules/layouts are destroyed under Vulkan 1.3.
- [x] Record direct and indexed draws. Drawing requires the graphics queue, an active rendering scope, a bound same-device pipeline, viewport/scissor, and matching attachment formats/sample count. Index binding validates usage, type, alignment, offset and remaining draw range. Pool reuse clears cached pipeline/dynamic/index state. CPU command checks do not inspect GPU index contents or infer resource barriers.
- [x] Keep the implementation in focused `Pipelines/VulkanShaderProgram`, `Pipelines/VulkanPipelineLayout`, `Pipelines/VulkanGraphicsPipeline`, `Internal/VulkanPipelineUtils`, and `Commands/VulkanCommandListDraw` files. Device methods delegate creation; generic RHI headers expose no Vulkan or compiler types.
- [x] Add eight native-dispatch capture tests for shader lifetime/failure cleanup, reflection ownership and rejection, graphics state translation, pipeline failure cleanup, invalid descriptions, draw prerequisites/reset, attachment/device mismatch, and index bounds.
- [x] Add `Source/Shaders/Slang/RhiSmoke/Triangle.slang`, compiled by the existing pinned Slang rules. `SwimRhiSmokeShaders` is a build-only artifact target consumed by `SwimTests`, with no new test executable or runtime compiler dependency.
- [x] Add opt-in `RHI.Vulkan.Smoke.TrianglePixelsAndIndexedParity`: create an RGBA8 target, render/read back a procedural triangle, check interior/background pixels and +Y-up orientation, then render with a 16-bit index buffer and require byte-identical pixels. Missing shader compilation or desktop/GPU support fails the opted-in test.
- [x] Implement reflected fixed-count descriptor layouts/tables/writes and samplers, then upload and sample a 2D texture through this pipeline path. See the texture checkpoint below.
- [x] Add explicit vertex-input layout contracts and vertex/instance buffer bindings, retaining empty layouts for shader-generated vertices. See the 2026-09-07 explicit vertex-input checkpoint below; native desktop validation remains open.
- [x] Implement reflected graphics push-constant ranges and recording, including partial writes and layout compatibility. See the 2026-09-07 push-constant checkpoint below.
- [x] Implement compute pipelines/dispatch with reflection, storage-buffer bindings, synchronization and readback coverage. See the 2026-09-08 compute checkpoint below.
- [ ] Implement additional query consumers at their renderer checkpoints. Timestamp queries are already implemented; other query types remain unsupported.
- [x] Run the real clear/triangle/texture smoke with clean validation on Windows and Linux, including resize/minimize/restore, before closing item 39 and starting RenderGraph. *(Done: all nineteen item 39 smokes pass the four validation profiles on Windows and Linux; see [the item 39 record](validation/Item39-2026-09-12.md).)*

**Validation:** the GCC 13/C++20 Debug foundation build passes **101 cases / 688 checks**, including all eight new pipeline/draw cases and the existing shader compiler/reflection cases. Platform/Input, RHI, Vulkan factory, and ShaderCompiler public-header gates compile. The actual SHA-256-verified Slang `2026.16.1` SDK compiles the new shader to SPIR-V with `vertexMain` and `fragmentMain` entry points. The architecture verifier passes. Both opted-in real-driver smoke cases were attempted and fail at SDL initialization (`No available video device`) before GPU work; no GPU pixel, validation-layer, or full Windows/MSVC engine success is claimed. Asset compiler and concrete physics backends were excluded from this foundation build.

**Delivery:** use the complete repository ZIP in a fresh directory. Retired sources already reside under top-level `Deprecated/`, outside every active source glob. The previous delivery-specific deletion scripts, move ledger, and CMake tombstones are removed. Normal build scripts remain the only build workflow. Build outputs, dependency caches and Python bytecode are not part of the source archive.

Implementation references: [Khronos graphics pipeline creation](https://docs.vulkan.org/refpages/latest/refpages/source/VkGraphicsPipelineCreateInfo.html), [dynamic-rendering pipeline formats](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineRenderingCreateInfo.html), and [draw command requirements](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdDraw.html).

### RHI reflected texture implementation checkpoint — 2026-09-05

This checkpoint continues item **39** from the latest uploaded repository, preserving its MSVC conforming-preprocessor test fix and command-parser regression coverage. The modern RHI now has the clear, procedural/indexed triangle, and sampled 2D texture implementation paths. Item 39 remains unchecked until real Windows/Linux validation and window-lifecycle coverage pass.

- [x] Create descriptor-set layouts from program-owned schemas. Sparse spaces produce valid empty set layouts for gaps. Validate duplicate spaces/bindings, positive fixed counts, supported descriptor types, explicit vertex/fragment visibility, physical-device per-stage/aggregate limits, and native layout support before creation. Partially created layouts are released on failure.
- [x] Retain native pipeline-layout/set-layout ownership through a shared internal state held by graphics pipelines and descriptor tables. Native layout handles remain alive for the full pipeline/table lifetime. Public `PipelineLayout::GetProgram` and `DescriptorTable::GetLayout` still require those originating public objects to outlive the referring object; no dangling-reference ownership contract is implied.
- [x] Implement descriptor-table allocation for samplers, sampled textures, uniform buffers, and read-only storage buffers. Each table initially owns one descriptor pool/set; pool pooling remains later work. Unsupported writable storage, acceleration-structure and variable-count descriptors reject explicitly. *(Item 45 added partially bound/update-after-bind sampler and sampled-texture arrays in explicit layout spaces; bindless allocation policy lives in `Render::BindlessResourceTable`.)*
- [x] Validate an entire write batch before updating any native descriptor or initialization state. Check reflected binding/array bounds, exactly one resource of the expected type, same-device ownership, buffer usage/alignment/range limits, and sampled image usage/view/sample/format compatibility. `BufferRange == 0` resolves to the remaining buffer range. Sampled views currently require single-sampled, filterable floating/normalized **2D color**; depth, integer, array-view and multisample sampling need later contract coverage.
- [x] Make tables immutable after their first recorded binding. Require every array element initialized before binding, then permit read-only reuse across command lists. Resource changes allocate a new table; retain the old table/resources until GPU completion through normal frame retirement. Writes and first binding require external host synchronization. This prevents ordinary descriptor updates from invalidating already-recorded Vulkan command buffers without introducing update-after-bind flags prematurely.
- [x] Implement normalized-coordinate samplers with nearest/linear min/mag/mip filtering, repeat/mirror/clamp addressing, finite supported LOD/bias, transparent-black float border, and bounded native sampler allocation accounting. Native failures release their reservation. Anisotropy and comparison sampling reject until the corresponding device/resource contracts are implemented.
- [x] Bind complete same-device tables to the currently selected graphics pipeline. Tables must originate from the same layout identity and match the requested space. Pipeline binding clears cached table bindings; all nonempty reflected spaces must be rebound before drawing. Pool reset also clears the command's descriptor state. Image/buffer barriers remain explicit caller work.
- [x] Add a tool-side `BuildRhiShaderInterface` conversion from parsed Slang reflection into owned RHI schemas. It preserves compiler set/binding numbers and conservatively exposes globals to the program's vertex/fragment stages. Supported flat resources are samplers, float32 2D sampled textures, uniform buffers, and read-only structured/byte-address buffers. Nested parameter blocks, descriptor-array type expansion, unsupported shader stages/access/shapes and duplicate slots return an error with no partial interface. Global push-constant buffers are added in the following 2026-09-07 push-constant checkpoint. Slang/simdjson/compiler code remains outside the runtime RHI target.
- [x] Add `RhiSmoke/Texture.slang`, compiled through the existing pinned Slang artifact target. `SwimTests` reads its actual reflection sidecar to construct the layout; descriptor positions are not duplicated in the smoke's C++ setup.
- [x] Add opt-in `RHI.Vulkan.Smoke.ReflectedTexturesAndTableReplacement`: upload two distinct 2x2 RGBA patterns, use separate immutable tables with sparse set 2, render each to a 16x16 target, read back after timeline completion, and compare every channel of every pixel against the expected nearest-sampled texels. This checks resource replacement and texture orientation without the legacy renderer or asset pools.
- [x] Add ten normal test cases across descriptor/sampler native capture and shader-interface conversion, including actual compiled reflection, failure cleanup, sparse sets, array initialization, write-batch rejection, buffer bounds/device ownership, draw prerequisites, immutability and retained native layout lifetime. Existing cases are updated for the newly supported descriptor layouts.
- [x] Implement resize/minimize/restore coverage. See the window-lifecycle checkpoint below.
- [x] Run all four smoke cases with clean Vulkan validation on Windows and Linux, then close item 39 before starting RenderGraph. *(Done: all nineteen item 39 smokes pass the four validation profiles on Windows and Linux; see [the item 39 record](validation/Item39-2026-09-12.md).)*
- [ ] Extend resource reflection and binding contracts for nested parameter blocks, runtime-sized arrays of resource classes other than samplers/sampled textures (item 45 covers those two), variable descriptor counts, additional sampled-image shapes/numeric classes, broader storage-image shapes/formats and graphics-stage stores, nested entry-point scope containers, automatic vertex-input reflection/interface validation and additional query consumers when their roadmap work begins. The current Slang sidecar does not encode every shader/sampler semantic; callers must still provide shaders and resources compatible with this baseline.

**Validation:** a real GCC 13/C++20 Debug foundation build passes **111 cases / 783 checks**, including the ten new cases. The pinned Slang `2026.16.1` compiler builds the texture SPIR-V and reflection sidecar; the default suite successfully converts that actual sidecar. Platform/Input, generic RHI, Vulkan factory and ShaderCompiler public-header gates compile, and the architecture verifier passes. All three opted-in GPU smoke cases were attempted and stop at SDL initialization (`No available video device`) before native GPU execution. Full Windows/MSVC engine and GPU pixel/validation success are not claimed here; the latest upload's Windows test-build fixes are preserved. Asset compiler and concrete physics backend builds were excluded from this foundation run.

**Delivery:** complete source repository ZIP for extraction into a fresh directory; normal build scripts only. No cleanup scripts, move ledger, CMake tombstones, dependency caches or build artifacts are added. The retired archive remains outside all active build globs.

Implementation references: [Khronos descriptor update lifetime rules](https://docs.vulkan.org/refpages/latest/refpages/source/vkUpdateDescriptorSets.html), [descriptor-set layout creation](https://docs.vulkan.org/refpages/latest/refpages/source/VkDescriptorSetLayoutCreateInfo.html), and [sampler creation](https://docs.vulkan.org/refpages/latest/refpages/source/VkSamplerCreateInfo.html).

### RHI window lifecycle implementation checkpoint — 2026-09-05

This checkpoint continues item **39** from `Swim-Engine(2).zip`, preserving the user's consolidated `SwimEngine` project. Engine code remains under its existing physical directories and compiles directly into the consuming executable. No engine-module library, alias, solution folder, migration script, or deprecated-path CMake exclusion is reintroduced.

- [x] Define explicit acquisition outcomes: `SwapchainAcquireResult::HasImage()` is required before using `ImageIndex`. A default result has no image. `Suspended`, `NotReady`, and `OutOfDate` never expose a usable index or signal the caller's acquire synchronization objects. A suboptimal result **does** own an image and must be submitted/presented before replacement.
- [x] Bound native acquisition to 16 ms, returning `NotReady` for timeout/not-ready so the caller can pump window events and retry without submitting a wait on an unsignaled semaphore.
- [x] Allow initial swapchain creation while minimized to return a dormant object with no images. Zero requested extent, minimized windows, zero drawable pixels, or zero native surface extents suspend acquisition without GPU waits or image destruction. `Resize` now returns `false` while suspended and `true` after a usable replacement; positive resizes rebuild even at unchanged dimensions. Native surface capabilities are queried before bootstrap to cover events that have not reached SDL yet.
- [x] Track acquired-image ownership per native generation. Reject presentation of an unacquired/out-of-range image, duplicate wait semaphores, and queues/synchronization objects from another device before dispatch. A suboptimal acquire or out-of-date/suboptimal present blocks further acquisition until replacement. Already acquired images can still be presented after suspension/invalidation; live replacement rejects outstanding acquired images.
- [x] Wait the supplied same-device frame retirement point and the existing presentation-queue fallback **before** replacement. After calling the builder with `oldSwapchain`, discard the old native generation on both success and failure: Vulkan retires it even when native creation fails, so retry must never acquire from or pass that retired handle again. New images/views are cleaned up on reported enumeration/view failures and wrapper-construction exceptions. Generic timelines and the public RHI remain backend-free.
- [x] Add `FrameContextRing::CancelFrame()` for an active frame with no command lists or pending retirement objects. Skipping acquisition reuses that frame slot without a queue submission, timeline increment, or release of objects awaiting retirement. Recorded frames and frames holding retirement objects must follow their normal submission/drain path.
- [x] Keep responsibilities in focused units: `VulkanSwapchain.cpp` owns the window/native images and replacement; `Internal/VulkanSwapchainSession` owns acquisition/presentation state; `VulkanSwapchainRetirement.cpp` owns the retirement wait. All use the existing recursive source discovery inside `SwimEngine` and `SwimTests`.
- [x] Add backend-neutral asynchronous `Window::Minimize()` / `Restore()` requests. Callers pump events and observe the resulting window state; a successful request is not evidence that the window manager has completed it.
- [x] Add seven native-dispatch capture cases and three frame-cancellation cases. They cover dormant/minimized/restored generations, bounded timeout/retry, suboptimal-image consumption, acquire/present invalidation, blocked retired-generation acquisition, foreign queues/invalid indices/duplicate waits, fatal native errors, frame-slot reuse, and retirement preservation. These run without SDL video or a GPU; they do not emulate native swapchain allocation or a compositor.
- [x] Add opt-in `RHI.Vulkan.Smoke.ResizeMinimizeRestore`: create while minimized, restore and present, resize through landscape/portrait/original sizes, explicitly suspend at zero extent, repeatedly minimize/restore, and rebuild at the same size. Each stage requires six stable presentations. Window transitions and rendering have deadlines; each replacement recreates per-image present semaphores after old-generation retirement. The clear/transfer smoke also handles bounded acquisition and rebuild requests.
- [x] Reconcile stale sections of `verify-build-layout.py` with the existing engine-module collapse. Checks now require the consolidated source lists, direct consumer SDK dependencies, gated physics/RHI sources, and actual test/header units. Fix README descriptions that still claimed Platform/Input were separate engine projects. Public-header scans and isolated compile gates continue to enforce implementation boundaries.
- [x] Execute clear/triangle/texture/window smoke on Windows and Linux desktops with the full Vulkan baseline and validation layers. Record adapter/driver, window system, all four results, and clean validation diagnostics before checking item 39 or starting RenderGraph. *(Done: all nineteen item 39 smokes pass the four validation profiles on Windows and Linux; see [the item 39 record](validation/Item39-2026-09-12.md).)*

**Caller sequence:** pump events; begin a frame; acquire. When `HasImage()` is false, cancel the empty frame. Retry `NotReady` after pumping events; on `OutOfDate`, or when a suspended window becomes drawable, call `Resize` with current pixel dimensions and the last submitted retirement point. Only a `true` resize result permits rebuilding per-image presentation resources. When an image is acquired, consume its signal through submission and call `Present` even if acquisition was suboptimal. A `false` present requests replacement. All methods on one swapchain require external host serialization, and its window must outlive it.

**Failure and lifetime limits:** native allocation failures can leave the object without images; retry `Resize` before acquiring. Surface/device loss throws and requires recreating the affected surface/device. The supplied timeline must cover every old-view submission. The existing core-WSI presentation-queue idle fallback is retained; this checkpoint adds no swapchain-maintenance present fences or asynchronous presentation retirement. Actual compositor/presentation lifetime validation remains part of the open desktop gate. The following diagnostics checkpoint makes the smoke fail automatically on captured warnings/errors, unavailable required validation, or dropped messages. A headless test count still does not establish desktop GPU validation.

**Validation:** GCC 13/C++20 Debug foundation build with the real pinned SDL3, GLM, mimalloc, enkiTS, Vulkan-Headers/volk/vk-bootstrap/VMA, and Slang `2026.16.1` compiles and links `SwimTests`. All nine available public-header/backend-contract gates compile. The default suite passes **121 cases / 872 checks** and the build-layout verifier passes. All four opt-in GPU smoke cases were attempted; each stops at SDL initialization with `No available video device`, before native GPU execution. Asset compiler, concrete Jolt/PhysX backends, and the legacy Windows executable were excluded from this foundation build. No Windows/MSVC or desktop GPU validation success is claimed.

**Delivery:** complete source repository ZIP for extraction into a fresh directory, with the guide and normal build/test workflow included. Build outputs, dependency caches and Python bytecode are excluded. No sources were retired in this checkpoint, so `Deprecated/` is unchanged.

Implementation references: [Khronos swapchain replacement/retirement rules](https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainCreateInfoKHR.html) and [bounded image acquisition and synchronization rules](https://docs.vulkan.org/refpages/latest/refpages/source/vkAcquireNextImageKHR.html).

### RHI diagnostics implementation checkpoint — 2026-09-05

This checkpoint continues Phase 9 / item **39** from `Swim-Engine(3).zip`. The next implementation work is observable Vulkan diagnostics and a strict desktop validation harness. The consolidated engine project and the existing source/dependency layout are preserved.

- [x] Add backend-neutral `GraphicsSystemDesc` with `ValidationMode::{Default, Disabled, IfAvailable, Required}`, a shared `DiagnosticLog`, and configurable console echo. `GraphicsFactory::Create` forwards the descriptor to the registered backend; existing direct creation calls use default options. Default requests validation in Debug and disables it in other configurations. Explicit Required works in every configuration and refuses creation unless both the Khronos validation layer and debug-utils capture can be enabled.
- [x] Add an owned, thread-safe diagnostic log. Callback text and IDs are copied; storage defaults to 256 messages, caps requested capacity at 4096, and bounds each stored ID/text to 256/8192 bytes. Warning/error/drop counters continue advancing after storage fills. `Record` never propagates allocation/locking exceptions into the driver. Snapshots own their data. There is no clear/reset operation that can erase failures during a validation run; use a new log for a new run.
- [x] Replace the default bootstrap callback with `VulkanDiagnosticCallback`. It captures general, validation and performance warnings/errors, preserves message IDs, tolerates missing callback fields, returns `VK_FALSE`, and makes no Vulkan calls or arbitrary application callbacks. The instance owns callback state through native destruction; callers can retain the shared log to inspect teardown. Console echo defaults on and is disabled by the smoke wrapper, which prints one report.
- [x] Explicitly enable optional `VK_EXT_debug_utils` independently of validation so GPU tools can use annotations in non-validation builds too. Use the pinned volk **instance** dispatch table for this instance extension. Required mode rejects missing prerequisites; selected validation/messenger setup failures cannot silently fall back to an unvalidated graphics system. Bootstrap loader/instance/adapter failures leave useful log entries when a caller supplies a log.
- [x] Forward supported descriptor debug names to native buffers, images/views, samplers, shader modules, pipeline layouts, descriptor-set layouts/pools/sets, and graphics pipelines. Name devices, distinct queue handles, swapchains/backbuffers, command pools/lists, binary/timeline semaphores and fences with fixed infrastructure labels. VMA allocation names remain intact. Native names use owned null-terminated copies of string views; empty names or unavailable debug utils omit annotation. Naming failure records a warning while preserving the created resource.
- [x] Add backend-neutral `BeginDebugLabel`, `EndDebugLabel`, and `InsertDebugLabel`. Vulkan validates recording/pool generation, nonempty names without embedded NUL, finite normalized RGBA colors, and balanced nesting within each command list. `End` rejects open regions; rerecording after a pool reset clears label state. Backends without debug utils can omit native annotations; Vulkan still validates region balance. These are GPU-tool labels, not GPU timestamps or cross-command-list regions.
- [x] Query adapter driver properties through the Vulkan 1.3 baseline and report device name, API version, driver name/build description, vendor/device IDs, and the raw vendor-encoded driver version. Do not decode a vendor driver version as a Vulkan API version. Move adapter information queries into `Internal/VulkanAdapterInfo.cpp` instead of growing the adapter header.
- [x] Make all four opt-in smoke cases use Required validation and verify the graphics system reports it active. `VulkanSmokeDiagnostics` retains the log outside the function that owns all native resources, device and instance, then checks warnings/errors/dropped messages **after teardown**. Any nonzero count fails the case. Reports are also printed on earlier functional failures. Annotate every smoke command buffer with named debug regions.
- [x] Add three normal diagnostic-log cases, seven Vulkan diagnostic cases, and one factory forwarding case. Cover copied/bounded messages, concurrent callbacks and overflow counters, clean-result criteria, validation policy, callback lifetime/null fields, optional naming/dispatch and string-view bounds, actual resource/pipeline name forwarding, label state/translation/reset, driver properties, and factory option delivery. Extend the existing build-layout verifier to require the focused implementation/test units.
- [x] Run clear/triangle/texture/window smoke on Windows and Linux desktops with the required Vulkan baseline. Preserve the adapter report, all four passing results, and zero warning/error/drop totals. Only then close item 39 and begin RenderGraph. *(Done: all nineteen item 39 smokes pass the four validation profiles on Windows and Linux; see [the item 39 record](validation/Item39-2026-09-12.md).)*

**API usage:** create a shared `Rhi::DiagnosticLog`, place it in `Rhi::GraphicsSystemDesc::Diagnostics`, and pass the descriptor to `GraphicsFactory::Create(api, desc)` or the Vulkan factory. A missing log is allocated by the backend and exposed by `GraphicsSystem::GetDiagnostics()`. `IsValidationEnabled()` reports the selected active configuration. Retain the log until after all resources, device and graphics system have been destroyed, then inspect `Snapshot()` / `IsClean()`. Snapshots taken while callbacks are still running are observations in progress, not a final clean-validation certificate.

**Scope:** this does not implement GPU-assisted/synchronization-validation feature configuration, GPU timestamps, device-fault/crash-dump extensions, memory-budget telemetry, or swapchain-maintenance presentation fences. Those checkboxes remain open. Debug-utils names/labels are present for RenderDoc and similar tools, but no GPU-tool capture or desktop native execution is claimed here. Validation capture has no hidden filtering or allowlist for warning/error messages.

**Validation:** the real GCC 13/C++20 Debug foundation build compiles and links the pinned SDL3/GLM/mimalloc/enkiTS/Vulkan-Headers/volk/vk-bootstrap/VMA backend plus Slang `2026.16.1` artifacts. `SwimTests` passes **132 cases / 958 checks**. All nine available public-header/backend-contract gates compile, and `verify-build-layout.py` passes. All four strict opt-in smoke cases were attempted and fail at SDL initialization (`No available video device`), before a graphics instance can be created. Zero captured Vulkan messages in those failed runs is not validation evidence. Asset compiler, concrete Jolt/PhysX backends and the legacy Windows executable were excluded from this foundation build; Windows/MSVC remains unverified.

**Delivery:** complete clean source repository ZIP, preserving the consolidated `SwimEngine` project. Normal build/test scripts remain; no cleanup/migration scripts, retired-file ledgers, CMake tombstones, build outputs or dependency caches are added. No sources were retired during this checkpoint.

Implementation references: [Khronos debug-utils guide](https://docs.vulkan.org/guide/latest/extensions/VK_EXT_debug_utils.html), [debug callback lifetime/threading rules](https://docs.vulkan.org/spec/latest/chapters/debugging.html), and [native debug-region balance rules](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdEndDebugUtilsLabelEXT.html). Swim deliberately requires regions to balance within one command list, a stricter boundary than native Vulkan's queue-level allowance.

### RHI GPU timestamp implementation checkpoint — 2026-09-05

This completes the next open Phase 9 diagnostics feature while critical-path item **39** remains a desktop validation gate. It does not begin RenderGraph or port any of the legacy renderer.

- [x] Add backend-neutral `RhiTimestamps.h`: begin/end pipeline boundaries, per-query availability, explicit Ready/NotReady/Error readback status, and `TimestampInfo` with the native fractional nanoseconds-per-tick period and valid counter width. Duration conversion subtracts integer ticks before converting to floating point and masks wraparound, including the full 64-bit case. Invalid metadata, unavailable samples, and nonfinite results produce no duration.
- [x] Implement owned timestamp pools in `Backends/Vulkan/Queries/VulkanQueryPool.h/.cpp`. Device creation forwards through the focused factory; pools retain device state, copy debug names, name native query objects, and destroy only successfully created handles. Zero-sized pools, unsupported query types/queue roles and unsupported timestamp families fail creation explicitly.
- [x] Expose `Queue::GetTimestampInfo()` and pool precision. Cache physical queue-family properties in device state; validate the actual selected family's counter width and period. Adapter-wide `TimestampQueries` means at least one family can provide the RHI lifecycle; its integer `TimestampFrequency` is approximate. Always use queue metadata for creation decisions and pool metadata for accurate conversion.
- [x] Add `VulkanCommandListQueries.cpp` for reset and timestamp recording. Validate device/family identity, overflow-safe ranges, recording generation, rendering scope and timestamp boundary before native dispatch. `Begin` uses top-of-pipe and `End` uses all-commands synchronization2 timestamps. Query reset and writes are kept outside dynamic rendering.
- [x] Implement nonblocking 64-bit readback with 64-bit availability, without WAIT or PARTIAL flags. Preserve available samples in a NotReady batch; unavailable entries and every output on native error are cleared. Invalid read ranges fail before calling Vulkan.
- [x] Add portable clock-conversion and Vulkan dispatch regression coverage for fractional precision, wraparound, unavailable values, failed creation, owned names, per-family capabilities, graphics/compute recording, invalid state/ranges/families/devices, rendering-scope rejection, native payload layout and failed/partial readback.
- [x] Add strict opt-in `RHI.Vulkan.Smoke.TimestampReadbackAndReuse`. Execute a reset before polling unavailable queries; record, submit, wait and read eight reuse cycles on each supported queue role; finally execute another reset and check that old availability disappears. Graphics timestamps bracket a verified buffer copy; other supported roles exercise query markers because general transfer recording currently requires the graphics family. Report unsupported roles explicitly and fail if no role supports timestamps. Require validation and a clean post-teardown diagnostic snapshot, just like the existing smokes.
- [x] Run all five strict smoke cases on Windows and Linux desktops and retain the adapter/driver reports, timestamp results and clean validation totals. This environment's tests cannot establish native timestamp accuracy or desktop stability. *(Done: all nineteen item 39 smokes pass the four validation profiles on Windows and Linux; see [the item 39 record](validation/Item39-2026-09-12.md).)*

**Lifetime and readback contract:** a pool is associated with the queue family selected by `QueryPoolDesc::Queue`; aliased roles in that same family can use it. Keep the pool alive through GPU completion. Reset a query before its first write and every later write, and synchronize prior uses before reuse. The caller owns reset/write ordering and synchronization, just as it owns resource transitions; command recording is not evidence that GPU reset has executed. Never poll a new, unreset pool. Before reading a reused pool, prove that its latest reset executed (the normal path waits for the writing submission's frame timeline/fence). Availability alone may still describe an old submission while a new reset is queued. Do not race readback with another reset. Use a pool or disjoint range per in-flight frame and read completed results before recycling that range.

**Timing semantics:** compare ordered begin/end samples from one queue in one submission. Intervals must be shorter than one complete counter wrap; multiple wraps cannot be recovered. Zero elapsed ticks are valid for short intervals. These are GPU pipeline measurements; stage sampling may occur later than the requested boundary and is not calibrated CPU time, cross-queue synchronization, or a promise of isolated pass cost. Bracket complete rendering regions outside `BeginRendering`/`EndRendering`.

**Queue limitation:** Vulkan timestamp writes may be supported on a dedicated transfer-only family, but `vkCmdResetQueryPool` is not. This checkpoint deliberately reports such a family as unsupported for the complete RHI lifecycle. A transfer role aliased to a graphics/compute family is supported. A later transfer-queue profiling path would need an explicit supported reset mechanism and synchronization, rather than assuming nonzero `timestampValidBits` is sufficient.

Typical synchronous diagnostic use (normal frame profiling should consume completed frame slots instead of draining every frame):

```cpp
auto queries = device.CreateQueryPool({ Rhi::QueryType::Timestamp, 2, "frame timing", Rhi::QueueType::Graphics });
// Check creation and keep queries alive longer than frames/submissions.
auto frames = Rhi::FrameContextRing::Create(device);
frames->BeginFrame();
auto& commands = frames->CreateCommandList();
commands.Begin();
commands.ResetQueries(*queries, 0, 2);
commands.WriteTimestamp(*queries, 0, Rhi::TimestampStage::Begin);
// Record the measured commands, including complete rendering regions.
commands.WriteTimestamp(*queries, 1);
commands.End();
frames->SubmitCurrent();
frames->Drain();
std::array<Rhi::TimestampResult, 2> samples{};
if (queries->ReadTimestamps(0, samples) == Rhi::QueryReadStatus::Ready)
{
	const auto nanoseconds = queries->GetTimestampInfo().ElapsedNanoseconds(samples[0], samples[1]);
}
```

**Validation:** GCC 13/C++20 Debug foundation build compiles and links the pinned SDL3/GLM/mimalloc/enkiTS/Vulkan-Headers/volk/vk-bootstrap/VMA backend and Slang `2026.16.1` artifacts. `SwimTests` passes **143 cases / 1,065 checks**, including eleven new timestamp cases. All nine available public-header/backend-contract gates compile and `scripts/verify-build-layout.py` passes, including the new focused timestamp units and suites. All five strict opt-in smokes were attempted and fail during SDL initialization (`No available video device`), before Vulkan instance creation. Their zero Vulkan-message counts are not clean-validation evidence. Asset compiler, concrete Jolt/PhysX backends and the legacy Windows executable were excluded from this foundation build; Windows/MSVC and native timestamp execution remain unverified.

**Following checkpoint:** device-loss diagnostics is implemented below; desktop validation of item 39 remains required before starting item 40 (RenderGraph).

**Scope and delivery:** compute pipelines/dispatch, occlusion/statistics queries, calibrated clocks, automatic per-pass profiler UI, device-loss diagnostics, memory-budget telemetry and RenderGraph remain separate tasks. No legacy sources were replaced or retired in this checkpoint. Preserve the consolidated build, existing normal test/build scripts and top-level `Deprecated/`; add no cleanup scripts, migration ledgers, CMake tombstones or module subprojects. Deliver the complete clean repository ZIP without build outputs or downloaded dependencies.

Implementation references: [timestamp recording rules](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdWriteTimestamp2.html), [GPU query reset queue restrictions](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdResetQueryPool.html), and [result availability and stale-reset hazards](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetQueryPoolResults.html).

### RHI device-loss diagnostics checkpoint — 2026-09-06

This completes the next Phase 9 diagnostics implementation after GPU timestamps. Critical-path item **39** remains a native desktop validation gate; item **40** (RenderGraph) has not started.

- [x] Add backend-neutral `DeviceLostError` and retained `DeviceDiagnostics` snapshots. The first observed loss is sticky, copies a bounded operation name without allocation, preserves the native result, and survives destruction of the device without retaining native objects.
- [x] Centralize Vulkan loss observation and typed failure propagation in `Internal/VulkanDeviceLoss.cpp`. Cover resource/descriptor/pipeline creation, allocation and readback, command begin/end/reset, queue submission, fences/timelines, idle waits, acquire/present and swapchain rebuild errors. Normal timeout/not-ready/out-of-date results remain distinct from device loss; ordinary errors retain their existing failure contracts. Fence polling and host waits now raise ordinary errors instead of silently treating them as unsignaled/timeouts.
- [x] Reject subsequent fallible work once loss is observed. Queue submission and presentation recheck health after taking their native queue mutex. Failed frame submission does not commit a timeline value, and query readback clears stale output before raising the typed exception.
- [x] Add optional `VK_EXT_device_fault` negotiation and bounded fault capture in `Internal/VulkanDeviceFault.cpp`. `GraphicsSystemDesc::DeviceFaultDiagnostics` defaults on, but missing extension/feature support never excludes an adapter. Vendor binary dumps are neither enabled nor requested. Capture uses one counts call and one data call, caps address and vendor entries at 64 each, copies even unterminated native descriptions safely, and reports incomplete/truncated/query-failed results separately from the original device loss.
- [x] Capture once on the first observer, outside diagnostic locks and outside the validation callback. Concurrent errors cannot overwrite the first operation or start repeated native capture; full diagnostic logs still retain error/drop counters and the independent device snapshot. Native naming remains nonthrowing but records loss and stops further naming calls.
- [x] Add `Internal/VulkanDeviceRetirement.cpp`. A loss from allocation or polling does not establish retirement of earlier GPU uses. Before lost-device child teardown, perform one device-idle attempt while serializing all selected native queues, deduplicating aliased queue mutexes. Retain whether the attempt happened and its result; unexpected idle errors are logged as unconfirmed retirement. No retry loop or blanket exception masking is introduced into normal fallible work.
- [x] Clear unspecified outputs from failed native object creation before throwing. Successfully created descriptor pools, pipeline partial results and other native owners still unwind through their normal RAII destruction. Keep implementation in focused units within the consolidated engine build.
- [x] Add 16 deterministic tests for sticky/owned reports, ordinary status classification, typed wait/submit/WSI failures, no native retries, query output clearing, failed-output cleanup, frame retirement, optional feature negotiation, bounded fault data, capture errors, concurrent observers and full logs.
- [ ] Validate actual device-loss behavior and fault reports on supported Windows/Linux GPU drivers. Dispatch capture is not evidence of a physical GPU fault, native teardown behavior, or successful device recreation.

**Caller contract:** retain `device.GetDeviceDiagnostics()` before running GPU work. Catch `Rhi::DeviceLostError` at the application boundary, stop and join submission/recording workers, then release frame contexts and device children before releasing the device. Calling `Device::WaitIdle()` explicitly during lost-device shutdown attempts retirement once and still raises the typed loss; otherwise the first native child destructor performs the attempt without throwing. Retained snapshots remain available afterwards. No automatic device reset, recreation, resource replay, binary crash dump or crash UI is implemented. Some native failures can prevent successful device recreation.

`Lost` means a native failure was observed; reading the snapshot does not poll the GPU. `Operation` is the first observer, not necessarily the cause. A concurrent snapshot may report `Pending` until the first observer finishes collection. `Fault.NativeResult` describes the last fault query, separately from the original loss result. `Unsupported` also covers explicitly disabled diagnostics. Interpret `RetirementResult` only when `RetirementAttempted` is true; an unexpected result means retirement was not confirmed. Snapshot copying can allocate. Nonthrowing debug naming may record a loss while a factory finishes returning its resource; later fallible work rejects the lost device. Work already in progress can finish, which is why callers must join their workers before teardown.

**Validation:** GCC 13/C++20 Debug foundation build: **159 cases / 1,289 checks passed**, including the 16 new cases above. All nine public-header/backend-contract targets compile, and `scripts/verify-build-layout.py` passes. All five strict opt-in GPU smokes were attempted and fail at SDL initialization (`No available video device`), before Vulkan instance creation; zero native message totals are not clean-validation evidence. The foundation configuration uses the pinned dependency cache, shader compiler enabled, asset compiler disabled, Jolt disabled and legacy engine disabled. Concrete physics backends, the legacy Windows executable, Windows/MSVC, actual GPU fault capture and desktop execution remain unverified. Reconfigure before building so configure-time source globbing includes the new units and tests.

**Following checkpoint:** memory-budget telemetry is implemented below. Desktop validation of item 39 is still required before beginning RenderGraph. This checkpoint retires no legacy files. Preserve top-level `Deprecated/`, the consolidated engine targets and normal build/test scripts; add no module subprojects, cleanup/migration scripts, deprecation ledgers or CMake tombstones. Delivery is the complete clean repository ZIP, excluding generated builds and downloaded dependencies.

Implementation references: [device-loss lifetime rules](https://docs.vulkan.org/spec/latest/chapters/devsandqueues.html#devsandqueues-lost-device), [optional device-fault extension](https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_device_fault.html), and [fault query counts, data and completion rules](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetDeviceFaultInfoEXT.html).

### RHI memory-budget telemetry checkpoint — 2026-09-06

This completes the next Phase 9 diagnostics task from `Swim-Engine(5).zip`, preserving the current device-loss implementation and consolidated engine build.

- [x] Expose backend-neutral `Device::GetMemoryBudgetSnapshot()` and owned per-heap `MemoryHeapBudget` values. Include stable heap index, capacity, device-local/host-visible classification, VMA allocation and native block counts/bytes, usage/budget provenance and saturating headroom. Empty snapshots mean unavailable; an available zero-use allocator still reports its heaps.
- [x] Keep implementation in `Internal/VulkanMemoryBudget.h/.cpp`. Read immutable heap/type metadata and cheap allocator counters from VMA; query current `VK_EXT_memory_budget` estimates explicitly on demand. Driver values therefore refresh even while allocations and frames are idle. Do not add GPU waits, frame-control callbacks, worker jobs, global logs or hidden periodic polling.
- [x] Track the enabled memory-budget extension in device state alongside the flag actually passed to VMA. The advertised `Capabilities.MemoryBudget` describes driver-estimate support; allocator telemetry works without the extension. The extension remains optional.
- [x] Report provenance per heap. Valid driver budgets retain native usage verbatim, including usage exceeding budget and zero usage. Missing support/dispatch or invalid native budgets (zero or greater than heap capacity) use allocator block bytes and VMA's 80%-of-capacity heuristic. Compute that fallback without overflowing a 64-bit heap size. Never label VMA's cached or internally substituted values as fresh driver evidence.
- [x] Query once per caller request; the VMA counter read can additionally refresh VMA's internal budget cache according to its own policy. Avoid `vmaCalculateStatistics` and full allocator traversal. Sampling does not mutate the application's frame index, implement allocation admission/eviction policy or promise allocation success.
- [x] Preserve device-loss behavior: reject a previously lost device before allocator/native queries and recheck health after sampling. A concurrent observed loss cannot become an apparently healthy telemetry response. Snapshots already returned remain readable after resource/device teardown without keeping any native owners alive.
- [x] Add nine deterministic tests, including real VMA allocation/free over captured native calls, suballocations versus reserved blocks, driver refresh while idle, unavailable/unsupported cases, per-heap fallback, shared host-visible/device-local heaps, owned snapshots, loss during sampling, bounds and 64-bit arithmetic.
- [x] Add opt-in `RHI.Vulkan.Smoke.MemoryBudgetAllocationAndRelease`. Allocate two real buffers and a texture through the public device factories across four cycles; verify live allocation count/byte growth and return to baseline after release, print per-heap reports and require a clean validation snapshot after all native owners are destroyed. Retained empty blocks and changing/lagging driver estimates are deliberately allowed.
- [x] Run all six strict GPU smokes on Windows/Linux desktops and retain actual adapter/driver, memory, timestamp and post-teardown validation results. Dispatch capture and CPU-side VMA counters do not establish native GPU/driver behavior. *(Done: all nineteen item 39 smokes pass the four validation profiles on Windows and Linux; see [the item 39 record](validation/Item39-2026-09-12.md).)*

**Usage:** call `device.GetMemoryBudgetSnapshot()` when a renderer overlay or memory-pressure decision needs updated telemetry. Check `IsAvailable()`, inspect each heap's `Source`, and use `GetHeadroomBytes()` or `IsOverBudget()` without unsigned underflow. Snapshots own their values; retain one for comparison after freeing resources or destroying the device. Sampling can allocate host memory and can raise `DeviceLostError` when loss has been observed.

`AllocationBytes` is the total occupied VMA allocation size, which can include resource alignment requirements; `BlockBytes` is native memory reserved by this allocator, including space not occupied by allocations. Neither includes every allocation in the process. Driver `UsageBytes` and `BudgetBytes` are estimates for the process, not whole-system free/used VRAM or an allocation guarantee. A fallback budget is only a heuristic, and fallback usage excludes allocations outside this VMA allocator. Driver usage can exceed budget; preserve that information instead of clamping it. Do not infer fragmentation from the difference between block and allocation bytes.

Heaps are returned once each. `HostVisible` means at least one memory type in that heap is host visible, not that every resource there can be mapped. UMA and host-visible device-local heaps may have both flags; do not add those categories together as separate physical capacities. Concurrent allocation/free activity can change individual counters during sampling: these are telemetry samples, not transactionally consistent allocation inventories. Stop allocation activity if exact cross-counter comparisons are required. VMA's own allocation-policy budget cache continues to follow VMA's existing update rules; this checkpoint adds reporting, not frame-driven allocator policy.

**Validation:** GCC 13/C++20 Debug foundation build: **168 cases / 1,465 checks passed**, including nine new deterministic memory-budget cases. All nine public-header/backend-contract targets compile, and `scripts/verify-build-layout.py` passes. The six strict opt-in GPU smokes were attempted and all fail at SDL initialization (`No available video device`), before Vulkan instance creation; their zero native diagnostic totals are not validation-clean evidence. Configuration enables the shader compiler and uses the pinned dependency cache, with asset compiler, Jolt and legacy engine disabled. Windows/MSVC, concrete physics backends and actual native GPU allocation/budget behavior remain unverified. Reconfigure before building to discover the added implementation and test sources.

**Following checkpoint:** pipeline-cache support is implemented below. Persistent upload/readback arenas, HDR, GPU-assisted validation and RenderGraph remain separate work. Critical-path item **39** still requires desktop validation before item **40** begins. No legacy sources are retired by telemetry; preserve `Deprecated/` and add no cleanup/migration scripts, CMake tombstones, deprecation ledgers or engine module subprojects. Deliver the complete clean repository ZIP with source/docs/scripts only, excluding build outputs and downloaded dependencies.

Implementation references: [VMA statistics and block/allocation semantics](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/statistics.html), [Vulkan per-process budgets and validity rules](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceMemoryBudgetPropertiesEXT.html). The implementation and deterministic tests use the repository's pinned VMA `v3.4.0`.

### RHI pipeline-cache checkpoint — 2026-09-06

This completes the next Phase 9 bootstrap task from `Swim-Engine(6).zip`. Graphics pipeline creation now uses native cache data while preserving the consolidated engine build and the device-loss/memory-budget work.

- [x] Add one lazily initialized `VkPipelineCache` per device state. Every implemented graphics pipeline factory passes it to `vkCreateGraphicsPipelines`; no caller opt-in or alternate renderer is needed. Destroy it with the final device-state owner, before allocator/device destruction. Pipeline owners do not own or destroy the shared cache.
- [x] Add backend-neutral `Device::LoadPipelineCache(span)` and `GetPipelineCacheData()` contracts. Callers can persist owned exported bytes and seed a new device before its first pipeline build. Empty or rejected data leaves cold initialization available. Once initialization has been attempted, loading returns `AlreadyInitialized`; there is no silent replacement of a live cache.
- [x] Keep cache lifetime/use in `Internal/VulkanPipelineCache.h/.cpp` and persistence encoding in `Internal/VulkanPipelineCacheData.h/.cpp`. Generic RHI headers contain no Vulkan types, native structs or backend filesystem policy.
- [x] Validate a versioned, little-endian persistence envelope before passing opaque bytes to Vulkan. Bound the complete stored data to 64 MiB; check exact payload length, checksum, vendor/device IDs, raw driver version, cache UUID and the native 32-byte version-one header. Parse bytes without unaligned structure casts. Invalid/corrupt data and incompatible devices/drivers never reach native cache creation.
- [x] Make serialization all-or-nothing. Hold the export lock across the native size/data pair, bound allocation before reading, respect the actual written length, and return explicit empty/too-large/incomplete/failed results with no partial bytes. No unbounded retry loop, shutdown-time write, implicit disk path or GPU wait is added.
- [x] Preserve native internal cache synchronization with flags zero. Parallel pipeline builds use shared host locks; initialization/import/export use exclusive locks. Recheck device health after acquiring locks and before later native calls. The exporter can wait for host compilation, but does not wait for GPU execution.
- [x] Treat ordinary cache-creation failure as optional acceleration failure: discard unspecified output handles, record one warning, and use uncached compilation for the remainder of that device lifetime. Explicit load reports `Failed`. Native device loss still raises `DeviceLostError`, retains the first diagnostic and prevents further cache/pipeline work. Existing partial-pipeline cleanup remains intact.
- [x] Add 15 deterministic cases covering exact round trips, unaligned storage, corruption at every byte, all truncation lengths, size limits, compatibility, initialization reuse, native errors, device loss, shared build access, lock-wait loss and actual file persistence across destroyed/recreated device fixtures.
- [x] Add strict opt-in `RHI.Vulkan.Smoke.PipelineCachePersistenceAndReuse`: compile multiple pipeline states, export to a temporary file, destroy the first device, load the exact file into a second device, and compile/export again with validation required. Print cold/warm device/build/export durations for inspection without asserting cache hits or performance improvement. Existing triangle/texture smokes now also exercise the default native cache path.
- [x] Run all seven strict native GPU smokes on Windows/Linux desktops and retain adapter/driver, cache, memory, timestamp and post-teardown validation reports. The warm-cache smoke validates native creation and persistence; it does not claim warm-cache pixel parity or measured acceleration in this environment. *(Done: all nineteen item 39 smokes pass the four validation profiles on Windows and Linux; see [the item 39 record](validation/Item39-2026-09-12.md).)*

**Caller lifecycle:** create the device, attempt `LoadPipelineCache` with previously saved bytes, build pipelines normally, then call `GetPipelineCacheData` at an explicit checkpoint while the device is healthy. Save only `Ready` results and copy the bytes exactly. Exported data owns no native resources and remains valid after teardown. If a cache is missing, rejected or unavailable, compile normally. Unsupported RHI backends return `Unsupported` from load/export. Host allocation exceptions can propagate; catch device loss at the application's existing device boundary and join compilation/submission workers before destroying their owners.

The backend performs no filesystem operations. The application can use `Platform::FileSystem::GetCacheRoot()` for its chosen cache-file location and the existing IO/file abstractions to read/write the exported blob. Read with a size bound, write a new temporary file and publish it with the application's replacement policy only after a successful complete write. Keep one writer per cache file. The new tests exercise real file bytes and recreation; automatic startup/shutdown persistence in the old renderer is intentionally not added. A missing/truncated file should cause a cold start, not prevent rendering.

| Result | Caller behavior |
| --- | --- |
| Load `Loaded` | Native cache was initialized with validated data; this does not guarantee a driver cache hit. |
| Load `Empty`, `InvalidData`, `Incompatible` | Ignore the input and continue; the next pipeline build initializes an empty cache. |
| Load `AlreadyInitialized` | The load is too late for this device; keep using its existing cache or uncached fallback. |
| Load `Failed` | Native cache creation failed; subsequent pipelines use uncached compilation. |
| Export `Ready` | Persist the complete returned bytes. |
| Export `Unsupported`, `Empty`, `TooLarge`, `Incomplete`, `Failed` | Skip persistence and retain any previous useful cache file. |
| `DeviceLostError` | Follow the device-loss shutdown contract; do not retry cache work on the lost device. |

The envelope is 56 bytes followed by the unchanged native payload: 8-byte `SWIMPC01` magic, 32-bit schema version, vendor ID, device ID and driver version, 16-byte UUID, 64-bit payload size and 64-bit FNV-1a checksum. Integers are little-endian. The checksum covers metadata and payload except its own field; it detects accidental corruption and is not authentication. Load only trusted local cache files created through this API. Driver version matching is deliberately conservative, even when a driver might accept data across versions. A new cache format must use a new schema version. Native caches remain opaque and disposable, and must not be shipped as portable shader assets.

**Validation:** Linux GCC Debug build passed with 183 deterministic cases and 1,805 checks, all nine public-header/backend-contract compile gates, and `scripts/verify-build-layout.py`. Built with the shader compiler enabled and the asset compiler, Jolt backend and legacy engine disabled. All seven opt-in native smoke cases were attempted; each stopped at SDL initialization with `No available video device`. Their zero validation-message counts are not GPU validation evidence. Native cache persistence, timing and post-teardown validation remain unverified on desktop GPUs; Windows/MSVC verification also remains open.

**Following checkpoint:** persistent mapped upload arenas/rings are implemented below. Readback arenas remain open in Phase 9. Critical-path item **39** still requires desktop validation before **40** (RenderGraph). This checkpoint does not implement a high-level pipeline-object deduplication registry, pipeline libraries/binaries, cache merging, background precompilation, compute pipelines or automatic file management. No legacy files are retired. Preserve `Deprecated/` and the consolidated engine targets; add no cleanup/migration scripts, CMake tombstones or engine module subprojects. Deliver the complete clean repository ZIP without generated builds, dependency caches or test artifacts.

Implementation references: [pipeline-cache ownership, synchronization and header layout](https://docs.vulkan.org/spec/latest/chapters/pipelines.html#pipelines-cache), [native cache initialization rules](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineCacheCreateInfo.html), and [size/data serialization and incomplete results](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPipelineCacheData.html).

### RHI persistent upload-arena checkpoint — 2026-09-06

This completes the next Phase 9 allocation task from `Swim-Engine(7).zip`. Upload staging can now use one persistent, bounded allocation per frame context, with the existing frame timeline controlling reuse. Readback arenas remain the next allocation task.

- [x] Add `BufferDesc::PersistentMap`, `Buffer::GetMappedWriteSpan()` and `FlushMappedWrites(offset, size)`. Persistent mapping is explicitly requested for `CpuToGpu` buffers; unrequested/unsupported mapping returns an empty span. Invalid mapped memory preferences are rejected before native creation. Existing `Write`/`Read` callers retain their behavior.
- [x] Use VMA's sequential host-write and persistent-map flags together, retain the allocation-relative mapped pointer, and release the mapping with the allocation. Repeated arena allocation, writing and flushing performs no native map/unmap. Ordinary buffer writes can still use VMA's cache-maintaining copy helper on the same persistent allocation.
- [x] Add the backend-neutral `RhiUploadArena.h` contract: one owned buffer, fixed configured capacity, aligned non-overlapping slices, direct writable bytes, copy-in convenience, explicit prefix flushing, and explicit reset. Allocation exhaustion returns an empty optional without modifying the cursor or allocating spill buffers. Invalid size/alignment throws. Offset/padding arithmetic is checked without overflowing.
- [x] Align offsets to at least four bytes and to the adapter's uniform/storage buffer offset requirements when those usages are requested. Support transfer-source, vertex, index, uniform, storage and indirect upload usages; reject destination-only and unrelated usage bits. The upload contract permits GPU reads only, including storage-buffer use.
- [x] Add optional `FrameContextDesc::Upload` storage, disabled by default with capacity zero. Every enabled frame slot owns its own arena. `AllocateUpload`/`WriteUpload` require an active enabled frame. `SubmitCurrent`, including its explicit-submit overload, flushes the used prefix before the queue call. The slot resets only after its completion wait succeeds. No device-idle wait, automatic growth, per-upload buffer allocation or extra queue submission is introduced.
- [x] Preserve failure and cancellation behavior: a failed flush prevents submission; failed flush/submission leaves the active frame and used bytes available for a healthy-device retry without advancing its signal value. Failed completion waits do not expose/reset the slot. Empty-frame cancellation does not advance the timeline; frames with allocated upload bytes cannot be canceled. `Drain` waits submitted work before discarding current unsubmitted commands/slices and resetting arenas. Guard frame timeline overflow rather than wrapping completion values.
- [x] Flush non-coherent memory through `vmaFlushAllocation`, which aligns the range to the native atom size. Coherent memory skips native flushing. Separate VMA allocations isolate adjacent frame slots' non-coherent atoms. Flush the used prefix on every submission attempt because writes through returned spans cannot be tracked individually.
- [x] Keep device-loss handling at the existing typed boundary. Known loss prevents mapping access, allocation and flushing; native flush loss raises `DeviceLostError`. Ordinary mapping failure releases native buffer/allocation state, and ordinary flush failure preserves host bytes. No lost-device retry/reset policy is introduced.
- [x] Add 15 deterministic cases: alignment/byte preservation, exact capacity, overflow, invalid descriptions, descriptor limits, frame-slot isolation, wait/flush/submit failure, cancellation, drain, partial construction, real VMA mapping reuse, coherent/non-coherent flushing, atom separation, cleanup and device loss. Move shared frame mocks into `Tests/Fixtures/RhiFrameCapture.h` so the new suite reuses existing frame behavior instead of duplicating it.
- [x] Add strict opt-in `RHI.Vulkan.Smoke.UploadArenaBufferTextureAndSlotReuse`: six submissions through two frame slots, nonzero aligned offsets, direct and copy-in writes, bounded exhaustion, buffer/texture transfers, timeline reuse, and exact readback comparison with required post-teardown validation.
- [x] Execute all eight native GPU smokes on Windows/Linux desktops and retain adapter/driver and validation evidence. Captured native calls exercise real VMA host logic but do not establish driver/GPU correctness. *(Done: all nineteen item 39 smokes pass the four validation profiles on Windows and Linux; see [the item 39 record](validation/Item39-2026-09-12.md).)*

**Lifetime and synchronization:** an arena is owned by one host thread or externally synchronized. Its slices share one completion lifetime. Standalone callers must flush after their last write and prove all GPU uses completed before `Reset` or destruction. Frame-ring callers finish writes before `SubmitCurrent`; returned spans become unwritable at submission and invalid at context reset, `Drain`, destruction or device loss. A stale raw span cannot be revoked by the API. Do not submit frame-owned slices independently, use them in later frames, or allow another queue to read them beyond the owning frame's completion point. Join host writers before flushing/submitting. Existing barriers and exclusive queue-family ownership contracts still apply; allocation does not add resource transitions or transfer queue ownership.

**Alignment and capacity:** alignment applies to the GPU buffer offset. The CPU view is bytes and does not promise alignment for typed placement construction. Use byte copies/direct byte writes and avoid reads from write-combined upload memory. For buffer-to-texture copies, supply the format's required texel/block offset alignment; four bytes is only the arena's baseline. Callers still satisfy transfer-size, descriptor-range and format requirements. Configured capacity is per slot; total upload reservation is approximately capacity times frame count plus allocator overhead. Exhaustion is explicit backpressure: size the next ring appropriately, split work across completed frames, or separately own/retire an exceptional staging allocation. Never reset an in-flight arena to make space. There is no spill/growth/ring-wrap policy hidden in `AllocateUpload`.

**Integration:** enable storage with `FrameContextDesc{ QueueType::Graphics, 2, { capacity } }`, begin a frame, allocate/write slices, record copies using `slice.Resource`, `slice.Offset` and `slice.Bytes.size()`, then submit normally. Uniform/storage users configure the arena usage accordingly. `FrameContextRing` performs the flush and retirement; standalone `UploadArena` users explicitly perform those steps. `VulkanUploadArenaSmokeTests.cpp` is a complete buffer/texture example. This establishes the modern RHI path without adding new upload policy to the legacy renderer.

**Validation:** Linux GCC Debug build passed with **198 deterministic cases and 1,963 checks**, all nine public-header/backend-contract compile gates, and `scripts/verify-build-layout.py`. The build enabled the shader compiler and disabled the asset compiler, Jolt backend and legacy engine. All eight opt-in native smoke cases were attempted and stopped at SDL initialization with `No available video device`; their zero validation-message counts are not GPU evidence. Real desktop buffer/texture round trips, Windows/MSVC builds and native post-teardown validation remain open.

**Following checkpoint:** readback arenas are implemented below; remaining Phase 9 capability and desktop validation work stays open. Critical-path **39** remains open before **40** (RenderGraph). Item **41** is partially implemented: upload storage and existing transfer primitives are available, while readback arenas and later scheduling/integration work remain open. No legacy sources are retired by this checkpoint. Preserve the consolidated engine build and `Deprecated/`; add no migration/cleanup scripts, CMake tombstones or engine module subprojects. Deliver the complete clean repository ZIP without build/dependency caches or generated test artifacts.

Implementation reference: [VMA persistent mapping and cache maintenance](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/memory_mapping.html). The repository remains pinned to VMA 3.4.0.

### RHI readback-arena checkpoint — 2026-09-07

This completes the next Phase 9 allocation task from `Swim-Engine(8).zip`. Bounded readback batches attach to the existing frame submission and preserve CPU results independently of frame-slot reuse.

- [x] Extend `BufferDesc::PersistentMap` to `GpuToCpu` allocations and add `GetMappedReadSpan()` / `InvalidateMappedReads(offset, size)`. Read mapping is const bytes; upload buffers do not expose read mappings, and readback buffers do not expose write mappings. Unrequested/unsupported mappings remain empty. Device-local persistent mapping and invalid ranges are rejected.
- [x] Use VMA's random host-access and persistent-map flags for readback buffers. Keep the mapping for the allocation lifetime. Invalidation uses `vmaInvalidateAllocation`, including non-coherent atom alignment and coherent-memory no-op behavior. Existing `Buffer::Read` remains available and can use the same persistent mapping without native remapping.
- [x] Add `RhiReadbackArena.h`: one fixed-capacity transfer-destination buffer, checked aligned allocation, opaque batch-scoped slices, nonblocking `TryGetData` / `TryRead`, and explicit `TryReset`. Exhaustion returns an empty optional without moving the allocation cursor or growing storage. Zero-sized requests and non-power-of-two alignment throw. Slices validate their batch identity; foreign, default and expired slices cannot read replacement data.
- [x] Attach readback arenas to both `FrameContextRing::SubmitCurrent` forms. Validate all batches and allocate retention bookkeeping before calling the queue. Reject null, duplicate, empty and already-submitted batches without sealing any of them. Successful submission commits the ring's actual timeline/value with no subsequent allocation or throwing bookkeeping. Caller-provided signals remain intact; there is no second submission or independent readback timeline scheduler.
- [x] Retain each submitted readback buffer in its frame context until completion, including when the caller destroys its arena early. Keep the completion timeline alive for retained CPU results after frame-ring destruction. Frame reuse and `Drain` release GPU retirement references without resetting readback arenas or discarding their results.
- [x] Return `NotSubmitted` before successful submission and `NotReady` before timeline completion. Never invalidate caches or expose a CPU span in either state. On the first successful read of a completed batch, invalidate its used prefix, then expose only the requested slice. Subsequent reads of that batch reuse the invalidation. Failed invalidation does not mark the batch ready and can be retried on a healthy device.
- [x] Make reset explicit and nonblocking: reject reclamation while the batch is in flight; allow reset after completion or when discarding unsubmitted commands. Reset reuses the allocation and invalidates old slice handles. CPU consumption is independent of GPU completion, so an unread result is kept until the caller explicitly discards it. There is no automatic frame-index expiration, implicit blocking read, spill allocation or overwrite of unread results.
- [x] Preserve typed device-loss handling. Mapping, reads, invalidation and reset recheck device health. Native invalidation loss raises `DeviceLostError`; subsequent operations do not keep calling the driver. `TryGetData` clears its output span before pending/error paths, and `TryRead` leaves the destination unchanged unless the complete slice is ready.
- [x] Add 17 deterministic cases for capacity/alignment/overflow, unsupported mapping, pending reads, stale/foreign/default slices, exact bytes, invalidation retry, slot reuse, retained timeline lifetime, early arena destruction, failed flush/submission, invalid batch lists, multiple batches/signals, real VMA persistent mapping, atom isolation, coherent memory, bounds, allocation cleanup and device loss. Rename the shared captured allocator fixture to `VulkanMappedBufferCapture`, reflecting its upload/readback role; no old fixture alias remains.
- [x] Add strict opt-in `RHI.Vulkan.Smoke.ReadbackArenaBufferTextureAndRetainedResults`: six submissions through two upload/frame slots and two readback arenas, nonzero offsets, exact buffer/pixel comparisons, reading results after frame-slot reuse, explicit reset, expired-slice rejection, and required validation through teardown.
- [x] Run all nine native GPU smokes on Windows/Linux desktops and retain adapter/driver and post-teardown validation evidence. Captured VMA tests prove host behavior, not native GPU execution. *(Done: all nineteen item 39 smokes pass the four validation profiles on Windows and Linux; see [the item 39 record](validation/Item39-2026-09-12.md).)*

**Caller lifecycle:** create the arena, allocate destination slices, record copies using `GetBuffer()`, `GetOffset()` and `GetSize()`, and record a final transition to `HostRead`. Pass every batch written by those commands to `SubmitCurrent`. The ring binds readiness to that successful submission's own completion signal. Poll `TryRead` for an owned copy or `TryGetData` for a const mapped view. Consume the result before explicitly resetting the batch. Readback arenas use the same device as the frame ring and recorded commands. They must not receive additional GPU writes after submission; another write requires completion and reset. The API does not infer which commands wrote which bytes or add resource/queue-family transitions.

```cpp
std::array batches{ readbackArena.get() };
frames->SubmitCurrent(batches); // All command lists owned by the current frame.
// Or: frames->SubmitCurrent(submitDesc, batches);

std::span<const std::byte> bytes;
if (readbackArena->TryGetData(slice, bytes) == ReadbackStatus::Ready)
{
	// Consume bytes before TryReset; copy them to retain data beyond this batch.
}
```

**Lifetime and capacity:** one arena is one externally synchronized host owner and one submission batch. Host readers must finish before reset/destruction. Slice handles are checked, but a previously returned raw span cannot be revoked and becomes invalid on reset, arena destruction or device loss. Early arena destruction discards CPU access; successful frame submission still retains its native buffer until GPU completion. Unsubmitted reset/destruction requires discarding any commands that reference the old slices. Capacity is explicit; keep multiple arenas for overlapping batches, poll completed work and release consumed results, or increase a later arena's configured size. No read/reset method waits for the GPU. Explicit frame waits/`Drain` retain their existing behavior.

**Cache and alignment:** all slices in one arena share the same completion point, so invalidating the used prefix cannot overlap still-running writes to another slice in that batch. Distinct arenas use separate VMA allocations and non-coherent atoms. GPU offsets align to at least four bytes; callers request any stricter texel/block alignment and satisfy the existing transfer-size/format rules. Mapped bytes carry no typed CPU alignment guarantee. These arenas are transfer destinations; direct GPU storage-buffer writes and automatic screenshot/file encoding are separate policies.

**Validation:** Linux GCC Debug build passed with **215 deterministic cases and 2,159 checks**, all nine public-header/backend-contract compile gates, and `scripts/verify-build-layout.py`. The shader compiler was enabled; asset compiler, Jolt backend and legacy engine were disabled. All nine opt-in native smoke cases were attempted and stopped at SDL initialization with `No available video device`. Their zero validation-message counts are not GPU validation evidence. Real desktop readback round trips, Windows/MSVC builds and native post-teardown validation remain open.

**Following work:** the Phase 9 HDR capability path is implemented in the following checkpoint; desktop validation remains open. Critical-path **39** must pass before **40** (RenderGraph). For **41**, upload/readback storage and direct buffer/texture transfer primitives are now implemented; graph-scheduled transfer integration remains separate. No legacy runtime sources are retired in this checkpoint. Preserve the consolidated engine build and top-level `Deprecated/`, without cleanup/migration scripts, aliases for renamed fixtures or CMake tombstones. Deliver the complete clean repository ZIP with no build outputs or dependency caches.

Implementation reference: [VMA persistent mapping and cache maintenance](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/memory_mapping.html). The repository remains pinned to VMA 3.4.0.

### RHI HDR swapchain capability checkpoint — 2026-09-07

- [x] Replace the unimplemented `SwapchainDesc::Hdr` boolean with `SwapchainColorMode::{Sdr, PreferHdr, RequireHdr}`. SDR remains the default. Prefer-HDR falls back only when no implemented HDR pair is advertised; require-HDR returns no swapchain rather than silently creating SDR. There is no obsolete flag alias or migration script.
- [x] Add backend-neutral `RhiSwapchainColor.h`, `Device::QuerySwapchainSupport(window)` and `Swapchain::GetColorSpace()`. Queries return a per-window presentation-support flag and owned format/color-space pairs, and perform no GPU wait. Unsupported backends return an empty snapshot. Vulkan query failures throw; known/native device loss retains the typed sticky `DeviceLostError` contract. A failed query is not misreported as lack of HDR support.
- [x] Enable optional instance extension `VK_EXT_swapchain_colorspace` when advertised. `GraphicsCapabilities::HdrSwapchain` reports the enabled backend negotiation path, not a monitor capability. Applications must query the concrete window. Absence of the extension keeps the SDR path usable.
- [x] Negotiate exact supported pairs in deterministic order: `RGB10A2Unorm` + HDR10 ST2084, `BGR10A2Unorm` + HDR10 ST2084, then `RGBA16Float` + extended linear sRGB. A matching `PreferredFormat` takes priority within the chosen mode. SDR supports BGRA/RGBA 8-bit sRGB and UNORM formats, favoring the requested format and then sRGB fallbacks. Other native pairs are omitted, rather than exposed with an unknown encoding. `BGR10A2Unorm` is appended to the format enum to preserve existing identifiers and has native conversion/transfer-size support.
- [x] Bound native format enumeration to four attempts and 4,096 entries; restart count/data enumeration on `VK_INCOMPLETE`, trim shrinking lists and discard incomplete results. Deduplicate pairs. Do not cache capabilities across display changes. Creation checks presentation/color support even when starting dormant; every drawable rebuild queries again before waiting for or retiring the old generation.
- [x] Verify the format **and** color space returned by vk-bootstrap against the selected pair. Its internal second enumeration can otherwise silently choose the first available pair. A mismatch destroys the new swapchain and requires another rebuild; it never exposes an unexpected SDR/HDR encoding. Failed selection preserves existing image objects but invalidates acquisition until a successful rebuild. Existing timeline/presentation retirement remains in place.
- [x] Keep surface ownership exception-safe for both capability queries and creation. Move device window/surface factories from `VulkanDevice.h` into `VulkanDeviceSwapchain.cpp`; keep color enumeration/selection in focused `Internal/VulkanSwapchainColor.h/.cpp`. No engine module subprojects or retired runtime dependencies are introduced.
- [x] Add 13 deterministic cases covering SDR defaults, strict/fallback HDR, preferred and unsupported pairs, optional extension gating, enumeration order/deduplication, BGR packing, post-build pair mismatch, presentation support, surface/display changes, incomplete/shrinking/empty/oversized lists, retry bounds, native failures and sticky device loss. Include the new public color header in the dependency-free RHI header gate.
- [x] Extend the shared native window lifecycle exercise with `RHI.Vulkan.Smoke.HdrNegotiationResizeMinimizeRestore`. It queries support, checks strict rejection on SDR-only surfaces, verifies the chosen pair after each rebuild, and presents through resize/minimize/restore with timeline retirement and required validation through teardown. HDR mode clears black, valid in both supported encodings; this does not validate tone mapping or luminance calibration.
- [x] Run all ten native RHI smoke cases on Windows/Linux desktops, including the strict HDR mode on an HDR-enabled display. Retain actual selected format/color space, adapter/driver and post-teardown validation output. A prefer-HDR smoke passing on an SDR display proves the fallback path only. *(Done: all nineteen item 39 smokes pass the four validation profiles on Windows and Linux; see [the item 39 record](validation/Item39-2026-09-12.md).)*

**Caller contract:** `PreferredFormat` is a preference, not a requirement. A default SDR pixel-format preference does not suppress HDR when requesting prefer/require HDR. Query capabilities to choose policy; creation still rechecks because the window can move between displays or OS settings can change. For an initially dormant swapchain, format and color space remain undefined until native images are created. Suspension retains the last native pair; always inspect both getters after a successful resize and rebuild format-dependent pipelines/output encoding when they change. A zero extent still suspends without creating a native zero-size swapchain. Live replacements still require the existing same-device retirement timeline and presentation of every acquired image first. After a failed resize, retry before acquiring; surface/device loss requires recreating the corresponding owner.

```cpp
const auto support = device->QuerySwapchainSupport(window);
SwapchainDesc desc{};
desc.ColorMode = SwapchainColorMode::PreferHdr;
auto swapchain = device->CreateSwapchain(window, desc);
// Handle null creation and a dormant zero-image swapchain before rendering.
// Once drawable, select the output encoding using GetColorSpace(), and the
// pipeline attachment format using GetFormat(). Re-read both after Resize.
```

**Scope:** the returned color space describes what the presentation engine accepts. It does not establish the physical panel's peak luminance or enable the operating system's HDR setting. ST2084 output requires BT.2020/PQ encoding; extended linear sRGB requires linear, extended-range sRGB output. UNORM SDR images need explicit sRGB encoding; sRGB attachment formats encode it on stores. The RHI does not tone-map scene color. Phase 17 retains HDR scene color, exposure/tone mapping, display luminance policy and optional `VK_EXT_hdr_metadata` mastering-metadata integration. No guessed mastering metadata is sent by this capability path.

**Desktop commands:** run the normal ten-smoke suite with `SWIM_RUN_RHI_SMOKE=1 SwimTests --filter=RHI.Vulkan.Smoke`. On an HDR-enabled desktop, set both `SWIM_RUN_RHI_SMOKE=1` and `SWIM_REQUIRE_HDR_SMOKE=1`; the HDR lifecycle case then requires an advertised HDR pair and fails if unavailable. On PowerShell, set `$env:SWIM_RUN_RHI_SMOKE='1'` and `$env:SWIM_REQUIRE_HDR_SMOKE='1'` before invoking `SwimTests.exe --filter=RHI.Vulkan.Smoke`. Exercise both PQ and linear-capable environments where available; the log records the chosen enum values. OS HDR toggles and moves between HDR/SDR displays also need manual coverage, with a resize/rebuild after each change.

**Validation:** Linux GCC Debug build passed with **228 deterministic cases and 2,240 checks**, including the 13 new color-negotiation cases / 81 checks. All nine public-header/backend-contract compile gates and `scripts/verify-build-layout.py` passed. The shader compiler was enabled; asset compiler, Jolt backend and legacy engine were disabled. All ten opt-in native smokes were attempted with strict HDR enabled and stopped at SDL initialization (`No available video device`). Zero reported validation messages before initialization are not GPU validation evidence. Real HDR presentation, OS display changes, Windows/MSVC builds and native post-teardown validation remain open.

**Following work:** critical-path **39** still requires real Windows/Linux desktop evidence before **40** (RenderGraph). GPU-assisted/synchronization-validation configuration is implemented in the following checkpoint. Item **41** still separates the implemented arenas/direct transfers from graph-scheduled integration. No legacy sources are retired here. Preserve the consolidated build and top-level `Deprecated/`; ship a complete clean repository with no generated build/dependency caches or cleanup/migration scripts.

Implementation references: [Khronos swapchain color-space extension](https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_swapchain_colorspace.html), [color-space and encoding definitions](https://docs.vulkan.org/refpages/latest/refpages/source/VkColorSpaceKHR.html), and [surface-format enumeration](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceSurfaceFormatsKHR.html). The implementation remains on the repository's pinned volk/vk-bootstrap 1.4.350 versions.

### RHI validation configuration checkpoint — 2026-09-07

- [x] Add backend-neutral `ValidationChecks` and `GraphicsSystemDesc::Checks` with independent synchronization and GPU-assisted options, both off by default. Explicit checks require validation and diagnostic capture even in Release or `ValidationMode::IfAvailable`. Combining checks with `Disabled` is rejected. Without explicit checks, existing Default/Disabled/IfAvailable/Required behavior is preserved.
- [x] Add `GraphicsSystem::GetValidationConfiguration()` to report validation plus the checks configured by the backend. Log both requested checks and the Khronos layer API version. This describes submitted configuration; it does not claim that an external layer override or every shader instrumentation path was verified.
- [x] Query loader capabilities through the supplied `vkGetInstanceProcAddr` before configuring the instance. Inspect only global extensions and `VK_LAYER_KHRONOS_validation` extensions; another disabled layer cannot satisfy the request. Debug utils supplied only by the validation layer are used only when that layer will be enabled. Bound property enumeration to four attempts and 4,096 entries, restart incomplete results, trim shrinking lists and report native query failures separately from absent support.
- [x] Use `VK_EXT_layer_settings` through the pinned vk-bootstrap `add_layer_setting` API. Pass dedicated BOOL32 settings `validate_sync`, `gpuav_enable` and `syncval_submit_time_validation`; explicitly configure both check switches and keep submit-time synchronization validation on. Setting values have immutable static lifetime through the deferred instance build. No deprecated `VK_EXT_validation_features` chain, reserve-binding-slot flag, legacy settings aliases or environment mutation is added.
- [x] Require a Khronos validation layer reporting API 1.4.335 or newer and available `VK_EXT_layer_settings` for explicit checks. This is Swim's supported settings baseline; no claim is made that all earlier layers lack these features. Missing layers, debug capture, settings support or the supported layer version fail the request with a diagnostic. Ordinary core validation continues to work with older layers without the settings extension.
- [x] Require `fragmentStoresAndAtomics` and `vertexPipelineStoresAndAtomics` in GPU-assisted physical-device selection, so unsupported adapters cannot be selected for that request. Logical-device creation carries these required features through vk-bootstrap. The normal Vulkan 1.3/timeline/buffer-device-address baseline remains in place. The validation layer reserves its descriptor set; existing pipeline-layout checks use the layer-reported `maxBoundDescriptorSets`. No extra descriptor-slot subtraction or forced robustness behavior is introduced.
- [x] Keep the implementation focused: `VulkanValidationSettings.h/.cpp` contains policy, setting values and device-feature requirements; `VulkanValidationCapabilities.cpp` contains loader enumeration; `VulkanInstanceDiagnostics.cpp` applies the selected policy and callback setup. Preserve the consolidated engine build. The new capture implementation follows the existing test layout with its declaration in `Tests/Fixtures` and implementation in `Tests/Suites/RHIVulkan`.
- [x] Add 15 deterministic cases covering all explicit-check combinations/request modes, missing capabilities, supported-version boundary, unchanged core behavior, invalid options, setting types/names/value lifetime, GPU-only feature requirements, configured-state reporting, loader/provider selection, absent functions, incomplete/shrinking/oversized lists, bounded retries, native errors and smoke-profile parsing. Captures call the real capability/configuration helpers without replacing vk-bootstrap's global loader, so they can coexist with real desktop smokes in one test process.
- [x] Add `SWIM_RHI_VALIDATION=core|sync|gpu|all` to the shared smoke runner. Every one of the ten existing native smokes checks the returned configuration against its requested profile and retains strict warning/error/drop checks through teardown. Invalid profile names fail instead of silently selecting core mode. `SWIM_RUN_RHI_SMOKE=1` remains the explicit registration opt-in; no extra test executable, cleanup script or CMake subproject is introduced.
- [x] Run the ten-smoke desktop suite in core, synchronization and GPU-assisted modes on Windows and Linux; retain selected adapter/driver, layer version, requested checks and post-teardown diagnostics. Run the combined profile where useful and retain strict HDR evidence on a supported desktop. Captured loader calls and configuration tests are not native instrumentation evidence. *(Done: all nineteen item 39 smokes pass the four validation profiles on Windows and Linux; see [the item 39 record](validation/Item39-2026-09-12.md).)*

**Runtime use:** set the checks on the same descriptor used to create the graphics system. A requested unsupported configuration returns no graphics system; GPU-assisted feature requirements can also leave no suitable adapter. Basic validation does not acquire the extra feature requirements. Keep the diagnostic log alive through teardown.

```cpp
GraphicsSystemDesc desc{};
desc.Validation = ValidationMode::Required;
desc.Checks.Synchronization = true;
desc.Checks.GpuAssisted = true;
desc.Diagnostics = std::make_shared<DiagnosticLog>();
auto graphics = factory.Create(GraphicsApi::Vulkan, desc);
// Handle creation failure, then inspect GetValidationConfiguration() and the log.
```

**Desktop profiles:** set `SWIM_RUN_RHI_SMOKE=1` and select one profile before invoking `SwimTests --filter=RHI.Vulkan.Smoke`:

| `SWIM_RHI_VALIDATION` | Synchronization | GPU-assisted |
| --- | --- | --- |
| unset / `core` | Off | Off |
| `sync` | On, including submit-time checks | Off |
| `gpu` | Off | On |
| `all` | On, including submit-time checks | On |

All profiles require capture. Following the September 12 desktop fixes, `core` and `sync` retain core validation, while `gpu` and `all` use Khronos's recommended separate GPU-AV pass with core checks off. Run `core` separately before the GPU profiles. GPU instrumentation and combined validation can increase execution time substantially. An opted-in smoke that exceeds its existing bounded deadline fails. It is not silently skipped. `SWIM_REQUIRE_HDR_SMOKE=1` remains independently available for the HDR lifecycle case.

**External settings and scope:** Vulkan Configurator, environment variables and layer settings files can override application configuration. Run acceptance tests without conflicting external overrides, and retain warnings about disabled/unsupported instrumentation rather than filtering them. `GetValidationConfiguration()` is not an introspection API for the layer's effective internal state. The backend supplies configuration and feature requirements; it does not implement validation algorithms or promise detection of every GPU error. Additional GPU-assisted features can still be unavailable and the layer can report that through the diagnostic log. Native negative-control/instrumentation verification, GPU-tool capture and actual desktop execution remain open. No SDK, validation-layer download or implicit machine configuration is added to the engine.

**Validation:** Linux GCC Debug build passed with **243 deterministic cases and 2,506 checks**, including 15 new cases / 266 checks. All nine public-header/backend-contract compile gates and `scripts/verify-build-layout.py` passed. The shader compiler was enabled; asset compiler, Jolt backend and legacy engine were disabled. All ten opt-in native smokes were attempted with `SWIM_RHI_VALIDATION=all` and stopped at SDL initialization (`No available video device`). Their zero validation-message counts are not GPU validation evidence. Windows/MSVC execution, actual shader instrumentation, submit-time synchronization checking and desktop teardown validation remain unverified.

**Following work:** critical-path **39** still requires actual Windows/Linux desktop evidence before **40** (RenderGraph). Run the documented desktop profiles and resolve any reported issues before closing that gate. Explicit vertex input is implemented in the following checkpoint. Graphics push constants are implemented in the later push-constant checkpoint. Compute/dispatch is implemented in the later compute checkpoint; broader descriptor work remains open as noted in the triangle/texture checkpoints; item **41** retains graph-scheduled transfer integration as open. No legacy runtime sources are retired here. Preserve top-level `Deprecated/`, consolidated engine sources and a complete clean repository ZIP without generated outputs or dependency caches.

Implementation references: [Khronos migration to modern layer settings](https://github.com/KhronosGroup/Vulkan-ValidationLayers/blob/main/docs/updating_from_VK_EXT_validation_features.md), [SDK 1.4.350 setting names](https://vulkan.lunarg.com/doc/view/1.4.350.0/windows/khronos_validation_layer.html), and [Khronos GPU-assisted requirements](https://github.com/KhronosGroup/Vulkan-ValidationLayers/blob/main/docs/gpu_validation.md). The repository's volk/vk-bootstrap pins remain 1.4.350.

### RHI explicit vertex-input checkpoint — 2026-09-07

This checkpoint completes the explicit vertex-input task from the triangle checkpoint using the latest uploaded repository. It extends the modern RHI graphics path to caller-provided vertex and instance streams. Critical-path **39** remains open for desktop validation; **40** (RenderGraph) has not started.

- [x] Add backend-neutral `VertexBindingDesc`, `VertexAttributeDesc` and `VertexInputRate` in `RhiVertexInput.h`. `GraphicsPipelineDesc` accepts binding/attribute spans, and `GraphicsCapabilities::VertexInput` reports device binding, attribute, stride and offset limits. Empty layouts preserve procedural shaders.
- [x] Translate layouts in focused `Internal/VulkanVertexInput` files. Validate counts, sparse slots, shader locations, duplicates, referenced bindings, input rates, attribute formats, native vertex-buffer format support, alignment and record extents before native pipeline creation. Graphics pipelines own their translated layout requirements; descriptor spans are only borrowed during creation.
- [x] Implement `BindVertexBuffer` in `Commands/VulkanCommandListVertex.cpp`. Validate recording state, device health/ownership, graphics family, slot, vertex usage, native handle and buffer offset before native recording. Invalid rebinding preserves the prior binding. Sparse bindings work before pipeline selection and survive pipeline switches and rendering-scope changes; command-pool reuse clears them.
- [x] Validate all used streams before direct/indexed draws. Direct draws check vertex and instance ranges including first-element offsets; indexed draws retain index-byte checks and validate instance ranges without reading GPU index contents. Overflow-safe arithmetic handles extreme draw arguments and 64-bit buffer sizes.
- [x] Add 13 deterministic layout/draw cases plus adapter-limit assertions. Cover native translation, descriptor lifetime, rejected formats/limits, exact attribute-tail bounds, zero stride, nonzero offsets, indexed behavior, stale generations, wrong queues, device loss, pipeline/scope persistence and arithmetic overflow.
- [x] Add pinned-Slang `RhiSmoke/VertexInput.slang` and opt-in `RHI.Vulkan.Smoke.VertexBuffersIndexedAndInstancedPixels`. The existing smoke shader artifact target compiles it; no new executable or module subproject is introduced. The test renders separate per-vertex/per-instance streams with normalized byte colors and checks both known pixels and full-image direct/indexed parity. Nonzero vertex/instance/index buffer offsets, first vertex/index/instance and a negative base vertex are exercised.
- [x] Run all **eleven** native RHI smokes on Windows and Linux desktops with the documented core, synchronization and GPU-assisted validation profiles. Retain adapter/driver, layer configuration and post-teardown diagnostics. Keep strict HDR coverage on an HDR-capable display. Only real passing evidence closes item 39. *(Done: all nineteen item 39 smokes pass the four validation profiles on Windows and Linux; see [the item 39 record](validation/Item39-2026-09-12.md).)*

**Layout contract:** slots identify buffers; locations identify shader inputs. The vertex shader must declare matching locations and numeric types. Instance rate advances once per instance; custom divisors are not exposed. Supported attributes are the RHI's uncompressed linear 8/16/32-bit normalized/integer/floating formats and supported packed color formats, subject to the adapter's vertex-buffer format features. Undefined, depth/stencil, compressed and sRGB formats reject. Component alignment is 1, 2 or 4 bytes (4 for packed formats). Attribute offsets, nonzero strides and bound offsets must meet that alignment. As a portable RHI restriction, every attribute must fit within a nonzero stride. A zero stride repeats one record, whose complete attribute range must still fit the buffer. Declared slots with no attributes do not require a bound buffer.

**Draw and lifetime contract:** bounds use the last accessed attribute byte, so trailing padding after the last record is not required. A draw with zero vertices/indices or instances performs no fetch-range arithmetic but still requires valid storage and bindings for the pipeline's used slots. Indexed vertex fetches depend on GPU index values and signed base vertex; keeping those effective indices in range is caller-owned. Automatic shader-interface matching and vertex-input reflection are not implemented. Buffer barriers and resource lifetimes remain explicit: retain bound buffers, the pipeline and other draw resources until GPU completion. Bindings do not add resource ownership or infer synchronization. This checkpoint does not port the transitional scene/mesh renderer.

**Validation:** Linux GCC Debug build passes **256 deterministic cases / 2,609 checks**. All nine public-header/backend-contract compile gates and `scripts/verify-build-layout.py` pass. The pinned Slang compiler builds the new SPIR-V, with vertex inputs reflected at locations 0–3. Shader compilation was enabled; asset compiler, Jolt backend and legacy engine were disabled. All eleven native smokes were attempted with `SWIM_RHI_VALIDATION=all`; they stop at SDL initialization (`No available video device`) before GPU execution. Zero captured diagnostics from those failed initializations are not validation evidence. Windows/MSVC, native pixel parity and actual GPU instrumentation remain unverified.

**Following work:** complete the item **39** Windows/Linux desktop gate before **40**. Graphics push constants are implemented in the following checkpoint. Compute pipelines/dispatch are implemented in the later checkpoint; broader descriptors and automatic vertex-interface reflection remain separate RHI tasks; **41** still includes graph-scheduled transfers beyond the implemented arenas/direct copies. No legacy files are retired here. Preserve the consolidated engine build and top-level `Deprecated/`; deliver the complete clean repository without generated outputs, dependency caches, cleanup/migration scripts or CMake tombstones.

### RHI reflected push-constant checkpoint — 2026-09-07

This checkpoint completes graphics push constants from the triangle/texture follow-up tasks, using the latest uploaded repository. Reflection, pipeline-layout creation, command recording and a Slang pixel consumer are implemented together. Critical-path **39** remains open for Windows/Linux desktop evidence before **40** (RenderGraph).

- [x] Extend tool-side Slang reflection to read a global `[[vk::push_constant]] ConstantBuffer<T>` from its **uniform element layout**. The outer `pushConstantBuffer` binding index is not a byte offset. Convert one global block into an owned RHI range with conservative visibility to the program's vertex/fragment stages. Missing/malformed byte extents, nonuniform layouts, zero/unaligned/overflowing ranges, multiple blocks and unsupported parameter types fail without partial interface output. Flat descriptors can coexist with the block.
- [x] Create native push-constant ranges from program-owned reflection in focused `Internal/VulkanPushConstants` files. Validate four-byte alignment, nonzero sizes, `MaxPushConstantBytes`, explicit supported/present stages and Vulkan's one-range-per-stage rule before allocating descriptor layouts. Different stages may have overlapping ranges. Canonical native range ordering makes identical range sets compatible across independently created layouts. Existing shared pipeline-layout ownership and failure cleanup also cover these ranges.
- [x] Add backend-neutral `CommandList::PushConstants(stages, offset, data)` for the currently bound graphics pipeline. Record inside or outside rendering on the graphics family. Reject stale recording generations, device loss, missing pipelines, empty/misaligned/out-of-range data and incorrect stage coverage before native recording. Every requested stage must cover the entire write; every range overlapping the write must have all its stages included. Rejected writes preserve existing state.
- [x] Track initialization per four-byte word and stage. Direct/indexed draws require all reflected range bytes initialized, including padding and zero-count draws. Partial updates preserve the other words. Binding pipelines or changing rendering scopes does not disturb values. Identical push-constant range sets preserve initialization across different pipeline/layout objects; the first write under an incompatible range set starts fresh initialization. Pool reuse clears all initialization. Command bookkeeping owns its range signature and stores no borrowed CPU data or layout pointer.
- [x] Add eleven native-dispatch capture cases for range ownership/translation, overlapping stages, device boundaries, absent/repeated stages, descriptor coexistence and native failure cleanup, byte-copy semantics, partial updates, compatibility, rejected writes, recording scope, pool reuse and device loss. Add five compiler cases for the actual pinned-Slang artifact, byte-layout extraction, malformed reflection, unsupported scopes and failure without partial output. Update the previous unsupported-push-constant regression to assert successful reflected layout creation.
- [x] Add `RhiSmoke/PushConstants.slang` and opt-in `RHI.Vulkan.Smoke.PushConstantUpdatesAndCompatiblePipelinePixels`. Four frames exercise full initialization before rendering, partial position/color updates inside rendering, retained scale through a distinct compatible pipeline layout, frame-slot reuse, known pixels and byte-identical direct/indexed readback. The existing Slang artifact target and `SwimTests` executable own this coverage.
- [x] Run all **twelve** native smokes on Windows and Linux with the documented core, synchronization and GPU-assisted validation profiles. Preserve adapter/driver, requested validation checks and post-teardown diagnostics. Retain strict HDR evidence separately on an HDR-enabled display. CPU capture tests and compiled artifacts do not close item 39. *(Done: all nineteen item 39 smokes pass the four validation profiles on Windows and Linux; see [the item 39 record](validation/Item39-2026-09-12.md).)*

**Using the contract:** derive `ShaderProgramInterface` with `BuildRhiShaderInterface`, create the program/layout/pipeline, bind that graphics pipeline, then pass the reflected stage mask and a byte span to `PushConstants`. The byte span is copied into command recording during the call and can immediately be reused or destroyed. Use the compiler's actual member layout for partial-write offsets; this conversion exposes the whole block extent, not generated C++ structs or member accessors. The new smoke checks its known C++/Slang packing explicitly. `ShaderStageMask::AllGraphics` includes unsupported stages and is rejected; use the reflected mask instead.

**Compatibility and initialization:** compatibility depends on identical push-constant ranges, including stage masks, offsets and sizes, independently of descriptor-set layout identity. Binding an incompatible pipeline alone leaves previous values intact, so rebinding the earlier compatible pipeline can use them. After any successful push with an incompatible range set, initialize that set fully before drawing; partial data from the prior set is not accepted by this RHI contract. This conservative rule avoids relying on undefined values or proving which bytes a shader actually reads. Declared range padding must be initialized too. Descriptor binding rules and caller-owned GPU resource lifetimes remain unchanged.

**Scope:** the implemented consumer is graphics on the graphics queue, with explicit vertex/fragment visibility. Runtime ranges can describe separate stage-specific extents; the current Slang conversion accepts one global uniform block and exposes it conservatively to every program stage. Scoped entry-point resources, multiple independently reflected blocks, compute pipelines/dispatch and other shader stages remain later work. CPU validation checks the provided interface and arguments; it does not prove arbitrary supplied SPIR-V matches reflection. No scene/material renderer port or legacy-source retirement is included.

**Validation:** Linux GCC Debug foundation build passes **272 deterministic cases / 2,790 checks**, including 16 new cases. All nine public-header/backend-contract compile gates and `scripts/verify-build-layout.py` pass. The pinned Slang compiler builds the new shader and its reflected 32-byte block; the compiled-reflection test verifies the resulting RHI range. Shader compilation was enabled; asset compiler, Jolt backend and legacy engine were disabled. All twelve native smokes were attempted with `SWIM_RHI_VALIDATION=all` and stopped at SDL initialization (`No available video device`). Zero captured diagnostics from these failed initializations are not native validation evidence. Windows/MSVC, GPU pixels, instrumentation and native teardown validation remain unverified.

**Following work:** complete critical-path **39** desktop validation before **40**. Compute pipelines/dispatch and their reflection, binding, synchronization and readback consumer are implemented in the following checkpoint; broader descriptors and automatic vertex-interface reflection remain open. Item **41** retains graph-scheduled transfer integration beyond the implemented arenas and direct copies. Preserve the consolidated engine sources and top-level `Deprecated/`. Deliver a complete clean repository without generated outputs, dependency caches, cleanup/migration scripts or CMake tombstones.

Implementation references: [Khronos push-constant command rules](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdPushConstants.html), [push-constant range limits](https://docs.vulkan.org/refpages/latest/refpages/source/VkPushConstantRange.html), and [pipeline layout creation](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineLayoutCreateInfo.html).

### RHI compute pipeline and storage-buffer checkpoint — 2026-09-08

This checkpoint completes the compute pipeline/dispatch task identified after push constants, using the latest uploaded repository. The modern RHI can execute a reflected Slang compute program, update storage buffers, synchronize dependent dispatches and copy results back to the CPU. Item **39** still requires real Windows/Linux evidence before **40** (RenderGraph).

- [x] Accept a single compute shader stage in `VulkanShaderProgram`; reject mixed graphics/compute programs and multiple compute stages. Extend the owned `ShaderProgramInterface` and its creation descriptor with `ComputeThreadGroupSize`, copied from Slang's fixed local size. Conversion requires exactly three positive uint32 dimensions, rejecting malformed/non-fixed sizes without partially initialized output.
- [x] Implement focused `Pipelines/VulkanComputePipeline.h/.cpp`. Validate same-device program/layout ownership, compute-only stage selection, matching entry name and local-size device limits before native creation. Check invocation products without overflowing. Empty `ComputePipelineDesc::EntryPoint` selects the program's sole compute entry; a supplied name must match. Reuse the device's native pipeline cache and locking, retain shared native-layout ownership, name pipelines and release partial objects on failure/device loss.
- [x] Expose `GraphicsCapabilities::Compute` with per-axis maximum group counts/local sizes and maximum local invocations. Dispatch counts are workgroups, distinct from reflected threads per group. Check each dispatch axis against the device limit; zero counts record a valid no-work dispatch but still require complete pipeline state.
- [x] Extend descriptor visibility and per-stage accounting to compute. Convert reflected `RWStructuredBuffer`/`RWByteAddressBuffer` to the existing `DescriptorType::StorageBuffer`. Keep read-only buffers distinct in the RHI even though both use native storage descriptors and share storage-descriptor budgets. Writable buffers require compute-only visibility. Validate buffer usage, alignment, ranges and entire write batches through the existing descriptor path. Tables remain immutable after recording; shader writes mutate buffer contents, not descriptors.
- [x] Implement `Commands/VulkanCommandListCompute.cpp`: compute-family capability checks, pipeline binding and dispatch outside rendering. The RHI has one active graphics/compute pipeline: binding either selects it and clears table bindings. Descriptors and push constants use the active layout. Draw after compute bind and dispatch after graphics bind require rebinding the correct pipeline. Pool reuse clears active state; rejected commands preserve valid recorded state.
- [x] Support compute push constants using existing range coverage and initialization rules. Compatible ranges preserve values; a successful push using incompatible ranges resets tracked initialization. Switching between graphics and compute pushes therefore requires initializing the newly selected ranges before draw/dispatch. Binding alone does not disturb values.
- [x] Allow buffer barriers on compute-capable families, including dedicated compute, using queue-supported all-command shader scopes and storage access masks. Reject vertex/index input states there. Existing buffer copies plus host/transfer/storage barriers support compute readback. Barriers retain ignored queue-family indices; no ownership transfers are implemented. Image work remains on the graphics family.
- [x] Add deterministic pipeline, descriptor, command and compiler coverage for native translation/cache/lifetime, local-size bounds/overflow, unsupported stages/accesses, descriptor limits and batch failures, incomplete state, stage switching, pool reuse, wrong queues, device loss, fixed-size parsing and the actual pinned-Slang artifact. Extend adapter capability assertions.
- [x] Add `RhiSmoke/Compute.slang` and opt-in `RHI.Vulkan.Smoke.ComputeStoragePassesAndReadback` to the existing smoke target/executable. Four frames initialize an output buffer, execute two dependent 3D dispatches with a read/write barrier and partial constant update, then verify every readback element. Three guarded tail elements remain unchanged. Changing input each frame and reusing two frame slots checks stale data and completion lifetime.
- [x] Run all **thirteen** native RHI smokes on Windows/Linux desktops with the documented validation profiles. Retain adapter/driver, validation requests and post-teardown diagnostics. Exercise dedicated-compute and graphics-aliased compute devices where available. CPU tests do not substitute for this gate. *(Done: all nineteen item 39 smokes pass the four validation profiles on Windows and Linux; see [the item 39 record](validation/Item39-2026-09-12.md).)*

**Runtime use:** compile a fixed-local-size Slang compute entry and convert its sidecar with `BuildRhiShaderInterface`. Pass **all three** interface members to `ShaderProgramDesc::Interface`: descriptor schemas, push-constant ranges and `ComputeThreadGroupSize`. Create the reflected layout and compute pipeline, initialize/bind tables and constants, then call `Dispatch(groupCountX, groupCountY, groupCountZ)` on a compute-capable queue outside rendering. Compiler metadata must match the supplied SPIR-V. The backend validates metadata/limits; it does not disassemble arbitrary bytecode to prove correspondence or infer shared-memory use. Specialized local sizes and automatic C++ parameter packing are outside this contract.

**Synchronization and lifetime:** dispatch inserts no barriers. Use `ShaderRead | ShaderWrite` for read-modify-write storage and an explicit same-state barrier between dependent passes. Transition output to `CopySource` before readback copies, transition the readback buffer to `HostRead`, and wait for submission completion before CPU access. Keep each buffer on one queue family; no ownership-transfer API or async-compute scheduler is introduced. Keep pipelines, tables and buffers alive through GPU completion. Public `PipelineLayout::GetProgram` and `DescriptorTable::GetLayout` retain their existing reference-lifetime requirements. The smoke uses the compute queue role, which may alias graphics, and drains before reusing host-written input.

**Scope:** storage images, graphics-stage stores/atomics, indirect dispatch, descriptor arrays/bindless reflection, scoped entry-point resources and extra shader stages remain separate work. Graphics-stage writable buffers reject because the baseline does not generally enable their optional store/atomic features. Fixed descriptors, samplers and sampled textures can be visible to compute, but existing image-shape and graphics-family transition restrictions still apply. The transitional scene/material renderer and legacy compute code are not ported by this checkpoint.

**Validation:** GCC 13.3/C++20 Debug passes **291 cases / 3,006 checks** (19 new deterministic cases). All **nine** public-header/backend compile gates pass, as does `scripts/verify-build-layout.py`. The pinned Slang `2026.16.1` compiler builds the actual compute SPIR-V/sidecar; conversion tests verify the fixed `(8,4,1)` local size, storage bindings and 24-byte constant block. This Linux foundation build enables Vulkan RHI and shader tooling; the asset compiler, Jolt backend and legacy engine are disabled. All **thirteen** native smokes were attempted with `SWIM_RUN_RHI_SMOKE=1 SWIM_RHI_VALIDATION=all` and stopped at SDL initialization (`No available video device`). No native GPU execution, readback correctness, validation instrumentation, Windows/MSVC or post-teardown GPU validation success is claimed. Zero captured diagnostics before initialization are not validation evidence.

**Following work:** complete item **39** desktop validation before **40**. Typed 2D storage images and compute-family image work are implemented in the following checkpoint. Remaining resource contracts include additional reflected image shapes/numeric classes, scoped resources, descriptor arrays/bindless policy and automatic vertex-interface validation. Item **41** still separates implemented direct transfers/arenas from graph scheduling. No legacy files are retired here. Preserve the consolidated engine build and top-level `Deprecated/`; deliver a complete clean repository without generated outputs, downloaded dependencies or cleanup/migration scripts.

Implementation references: [Khronos compute pipeline creation](https://docs.vulkan.org/refpages/latest/refpages/source/VkComputePipelineCreateInfo.html) and [dispatch requirements](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdDispatch.html).

### RHI typed storage-texture checkpoint — 2026-09-08

This checkpoint implements the next storage-image resource contract after compute pipelines. Reflected compute programs can bind typed 2D storage images, perform dependent image reads/writes, and transfer selected mip/layer results to the CPU on the same graphics or compute family. Item **39** remains open for desktop validation before **40** (RenderGraph).

- [x] Extend `DescriptorBindingDesc` with `StorageTextureFormat`, appended to preserve existing aggregate fields. Storage-texture bindings require an exact supported format; other descriptor types keep this field `Undefined`. The owned shader/program/layout schemas retain it.
- [x] Preserve Slang's explicit parameter `format` qualifier and scalar/vector component count in `ShaderBindingReflection`. Focused `ShaderStorageTexture.h/.cpp` converts the typed format contract. A compute `RWTexture2D` must have a supported qualifier, compatible 32-bit scalar type and exact component count. Missing/unknown formats, mismatched numeric classes, multisampling, array/3D/cube shapes, scoped resources and graphics-stage stores reject without partial interface output. No runtime Slang/JSON dependency is introduced.
- [x] Add native storage-image descriptor translation, pool allocation and separate per-stage/set limits. Storage images count toward total per-stage resources together with sampled images and buffers. Writable image bindings remain compute-only. Variable descriptor counts and partially bound arrays retain their existing restrictions.
- [x] Move image-write validation to focused `Descriptors/VulkanDescriptorImages.h/.cpp`. A storage write requires a same-device, nonnull 2D color view of exactly one mip and one layer, storage usage, one sample, the shader's exact format and native storage-image format support. Nonzero mip/layer selections are allowed. Storage descriptors carry a null sampler and `GENERAL` layout; they do not require filtering support. The sampled-image path retains its existing filtering policy. Whole-batch validation and table immutability remain intact.
- [x] Validate storage-image creation before VMA allocation through `Internal/VulkanStorageTexture.h/.cpp`: supported typed format, 2D image, one sample, mip count appropriate to the extent, native storage-format support, complete usage-combination support and native extent/mip/layer/sample limits. Other texture creation paths retain their existing behavior. 2D images may contain multiple mips/layers; each storage view selects exactly one of each.
- [x] Move image barriers/transfers into `Commands/VulkanCommandListImages.cpp`. Graphics and compute-capable families support color-image transfers and image barriers. Dedicated-compute barriers reject attachment/presentation states. Transfer-only families remain excluded from image work. Vulkan graphics/compute queues guarantee texel-granular transfers; no dedicated-transfer granularity or ownership-transfer API is introduced.
- [x] Add deterministic reflection, native descriptor, creation-query and command coverage: supported formats/numeric classes, exact format ownership, limits, malformed views and usage, format-feature rejection, failed batches, foreign resources, table sealing, device loss, nonzero subresources and dedicated-compute barriers/copies.
- [x] Add pinned Slang `RhiSmoke/StorageTexture.slang` and opt-in `RHI.Vulkan.Smoke.TypedStorageTexturesAndSubresourceReadback` within the existing smoke target/executable. The shader uses `rgba32f`, `r32ui` and `r32i` outputs. Four frames upload guard values, bind mip 1/layer 1 as 2D views, execute two dependent passes with explicit same-state barriers and a partial constant update, then verify float/unsigned/signed pixels and unchanged guard regions. Inputs change with each frame and two frame slots are reused.
- [x] Run all **fourteen** native smokes on Windows/Linux desktops with the documented validation profiles; retain adapter/driver and post-teardown diagnostics. Exercise both dedicated-compute and graphics-aliased compute queues where available. Default deterministic tests are not native GPU evidence. *(Done: all nineteen item 39 smokes pass the four validation profiles on Windows and Linux; see [the item 39 record](validation/Item39-2026-09-12.md).)*

**Typed formats:** the initial contract supports the qualifiers below. Native format support is checked independently; the qualifier and bound view format must agree exactly.

| Slang qualifier | RHI format | Shader element |
| --- | --- | --- |
| `r32f`, `r32ui`, `r32i` | `R32Float`, `R32Uint`, `R32Sint` | `float`, `uint`, `int` |
| `rgba32f`, `rgba32ui`, `rgba32i` | `RGBA32Float`, `RGBA32Uint`, `RGBA32Sint` | `float4`, `uint4`, `int4` |
| `rgba16f`, `rgba16ui`, `rgba16i` | `RGBA16Float`, `RGBA16Uint`, `RGBA16Sint` | `float4`, `uint4`, `int4` |
| `rgba8`, `rgba8_snorm`, `rgba8ui`, `rgba8i` | `RGBA8Unorm`, `RGBA8Snorm`, `RGBA8Uint`, `RGBA8Sint` | `float4`, `float4`, `uint4`, `int4` |

**Usage:** annotate the resource with an explicit qualifier such as `[[vk::image_format("rgba32f")]] RWTexture2D<float4> Output;`, compile and convert its sidecar, then pass all three shader-interface members as in the compute checkpoint. Create a matching `TextureUsage::Storage` image and a one-mip/one-layer 2D view. Bind it through `DescriptorWrite::TextureResource`. For image reads/writes through a storage descriptor, use `ShaderRead | ShaderWrite`, which maps to storage access in `GENERAL`. Plain texture `ShaderRead` means sampled-image access and a sampled-image layout. Writing a descriptor never transitions or initializes the image. The application must supply shader bytecode matching the reflection; metadata checks do not disassemble arbitrary SPIR-V or validate shader data races.

**Synchronization/lifetime:** dependent dispatches need explicit barriers, including `GENERAL`-to-`GENERAL` storage barriers when the logical state is unchanged. Transition output to `CopySource` for readback, then the destination buffer to `HostRead` and wait for completion. Keep images, views, tables and pipelines alive through submission completion. Every resource stays on one queue family; compute image support does not imply ownership transfer, cross-queue scheduling or automatic barriers. The smoke drains before reusing host upload data.

**Scope:** this is typed, single-sampled 2D compute storage-image access. Other image shapes/formats, formatless access, multisampled storage, image atomics, graphics-stage writes, descriptor-array/bindless reflection and scoped entry resources remain separate contracts. The transitional renderer and its legacy compute shaders are unchanged; no legacy files are retired here. Consolidated engine targets and `Deprecated/` remain the project structure.

**Validation:** GCC 13.3/C++20 Debug passes **304 deterministic cases / 3,337 checks**, including 13 new cases / 331 checks. All **nine** public-header/backend-contract compile gates and `scripts/verify-build-layout.py` pass. The pinned Slang `2026.16.1` compiler builds the actual storage-image SPIR-V/sidecar; tests verify its three exact typed formats, `(8,8,1)` local size and 24-byte constant block. This Linux foundation build enables Vulkan RHI and shader tooling; the asset compiler, Jolt backend and legacy engine are disabled. All **fourteen** native smokes were attempted with `SWIM_RUN_RHI_SMOKE=1 SWIM_RHI_VALIDATION=all`; every case stopped at SDL initialization (`No available video device`) before GPU execution. Native image/readback correctness, dedicated/aliased compute execution, actual validation instrumentation, Windows/MSVC and post-teardown GPU validation remain unverified. Zero captured diagnostics before initialization are not GPU validation evidence.

**Following work:** complete the item **39** Windows/Linux validation gate before **40**. Direct entry-point descriptors are implemented in the later entry-point reflection checkpoint. Remaining independent RHI resource tasks include broader sampled-image shapes/numeric classes, nested scope containers, descriptor-array/bindless policy and automatic vertex-interface validation. Item **41** retains graph-scheduled integration beyond direct transfers/arenas. Deliver a complete clean source ZIP with no generated build/dependency artifacts or cleanup/migration scripts.

Implementation references: [Khronos storage images](https://docs.vulkan.org/guide/latest/storage_image_and_texel_buffers.html), [Slang explicit image formats](https://docs.shader-slang.org/en/latest/external/slang/docs/user-guide/a2-01-spirv-target-specific.html), and [Vulkan queue-family transfer granularity](https://docs.vulkan.org/refpages/latest/refpages/source/VkQueueFamilyProperties.html).

### Storage-texture Windows soft-build correction — 2026-09-08

- [x] Correct the complete ZIP packaging: use fresh timestamps for every archived source file. The preceding ZIP preserved the old timestamp on the modified `VulkanCommandListTransfers.cpp`, so a timestamp-based incremental build could reuse its pre-split object alongside the new `VulkanCommandListImages.cpp` and report duplicate image-command definitions. The source contains only one definition of each image command, in `VulkanCommandListImages.cpp`; no cleanup script or CMake workaround is needed.
- [x] Convert the nonnegative `std::bit_width` result explicitly to `std::uint32_t` before comparing it with `MipLevels`, resolving MSVC C4018 without changing mip-limit validation.

**Recovery:** extract the corrected complete ZIP, replacing existing source files, then run the normal soft build. To repair an existing extraction immediately, update the timestamp on `Source/Engine/Systems/Renderer/RHI/Backends/Vulkan/Commands/VulkanCommandListTransfers.cpp` (for example by saving it) and rerun the soft build. Cached dependency sources do not need to be removed.

**Validation:** GCC/C++20 recompilation of the buffer commands, image commands and storage-texture validator passes with signed/unsigned comparisons treated as errors. A combined relocatable link passes with no duplicate definitions. The repository layout check passes, and the corrected archive is checked for complete contents and fresh timestamps. The preceding checkpoint passed 304 cases / 3,337 checks; that full suite is not rerun for this packaging/type-conversion correction. Native MSVC linking must be confirmed on Windows.

### RHI entry-point descriptor reflection checkpoint — 2026-09-08

This checkpoint implements direct entry-point descriptors from the resource-reflection follow-up list, using `Swim-Engine(20260908-194819).zip` as the baseline. The existing runtime RHI consumes the combined owned interface; shader-tooling metadata remains outside the runtime. Item **39** still requires desktop evidence before **40** (RenderGraph).

- [x] Convert flat `uniform` resource parameters on one compute entry or vertex/fragment entries through the same descriptor rules as globals. Preserve absolute Slang `(space, index)` coordinates. Entry descriptors use only their declaring stage; globals remain conservatively visible to every program stage. Reject multiple entries of the same graphics stage before exposing an interface.
- [x] Move the shared resource/type conversion into focused `ShaderDescriptorBinding.h/.cpp`. Uniform buffers, read-only buffers, sampled 2D float/normalized textures and samplers work in graphics or compute; writable buffers and typed 2D storage images remain compute-only. Existing format, shape and fixed-descriptor restrictions apply equally to both declaration locations.
- [x] Reject every duplicate `(space, binding)` across globals, entries or parameters within an entry, including matching types on disjoint graphics stages. Independent declarations do not imply resource aliasing. Same binding numbers in different spaces remain valid. A failure discards descriptors, push constants and compute local size together.
- [x] Preserve entry scope kind, unsupported scope/binding layout markers and stage semantics in reflection, appended after existing aggregate members. Prefer the structured `scope.parameters` list when present, so Slang's duplicate legacy list is not converted twice. Legacy `entryPoints[].parameters` sidecars remain supported when they carry flat descriptor coordinates.
- [x] Ignore recognized scalar/vector/matrix/struct stage IO only. Reject implicit uniform scope containers, nested parameter blocks, entry-local push blocks, uniform byte parameters, descriptor arrays, mixed binding categories, malformed binding counts/spaces and malformed entry parameter lists. No implicit uniform or descriptor can disappear because it was mistaken for stage IO. One existing global push-constant block continues to work alongside entry descriptors.
- [x] Add deterministic tests for stage visibility, absolute coordinates, collision rollback, supported resource classes, unsupported scopes/parameters and actual pinned Slang artifacts. `ScopedGraphics.slang` combines a shared global uniform, a vertex-local uniform, and fragment-local texture/sampler bindings. `ScopedCompute.slang` combines a global input/push block with entry-local writable and uniform buffers.
- [x] Add opt-in `RHI.Vulkan.Smoke.ScopedComputeDescriptorsAndReadback`: bind both reflected descriptor spaces, upload changing inputs and uniform data over four frames/two frame slots, execute dependent passes with an explicit storage barrier and partial push-constant update, and verify readback plus untouched guard elements. The existing shader target and test executable own the consumer; no new engine subproject is introduced.
- [x] Run all **fifteen** native smokes under the documented Windows/Linux desktop validation profiles and retain adapter/driver and teardown diagnostics. The new readback smoke supports a dedicated or graphics-aliased compute queue; actual GPU execution remains part of item **39**. *(Done: all nineteen item 39 smokes pass the four validation profiles on Windows and Linux; see [the item 39 record](validation/Item39-2026-09-12.md).)*

**Usage:** declare a direct entry resource as `[[vk::binding(5, 1)]] uniform RWStructuredBuffer<uint> Output` in the compute function's parameter list, compile the whole program, then pass the converter's descriptor schemas, push ranges and local size together to `CreateShaderProgram`. Allocate/write/bind each returned descriptor space through the existing RHI. Keep shared resources global and use unique coordinates for independent declarations. Do not add the global space/binding index to an entry's reflected coordinates: the pinned SPIR-V sidecar already reports their assigned locations.

**Container boundary:** `uniform uint Count` on an entry causes the pinned Slang compiler to introduce a scope container; this checkpoint rejects that layout. Use an explicitly bound `uniform ConstantBuffer<Settings>` descriptor instead, or the supported global `[[vk::push_constant]] ConstantBuffer<Settings>`. The compiler warns that `[[vk::push_constant]]` on a direct entry parameter is unsupported and ignores it; application code must follow the emitted descriptor interface, not the ignored attribute. Recursive struct/parameter-block flattening, implicit scope uniform packing and independently scoped push-constant blocks remain later work.

**Ownership/synchronization:** metadata conversion does not create resources, update descriptor tables or infer barriers. Existing immutable-after-bind tables, explicit resource states, same-family ownership and submission lifetimes apply. The smoke drains before rewriting host-visible inputs and retains all referenced resources through completion. Reflection does not prove arbitrary supplied SPIR-V matches its interface.

**Validation:** GCC 13.3/C++20 Debug passes **313 default cases / 3,549 checks**, including nine new cases / 212 checks. All **nine** public-header/backend-contract compile gates and `scripts/verify-build-layout.py` pass. The pinned Slang `2026.16.1` compiler builds both new SPIR-V/sidecar pairs; the compiled-reflection tests verify graphics visibility and the combined compute descriptors, local size and global push block. This Linux foundation build enables Vulkan RHI and shader tooling; asset compilation, Jolt and the legacy engine are disabled. All **fifteen** native smokes were attempted with `SWIM_RUN_RHI_SMOKE=1 SWIM_RHI_VALIDATION=all` and stopped at SDL initialization (`No available video device`). Windows/MSVC, native readback correctness, dedicated/aliased compute execution, actual validation instrumentation and GPU teardown validation remain unverified. Zero diagnostics before platform initialization are not native validation evidence.

**Following work:** finish item **39** desktop validation before **40**. Fixed descriptor arrays are implemented in the following checkpoint. Remaining independent resource tasks include broader sampled-image shapes/numeric classes, nested scope containers/parameter blocks, runtime-sized/bindless policy and automatic vertex-interface validation. Item **41** retains graph-scheduled integration beyond the implemented direct transfers/arenas. Preserve the consolidated engine build and top-level `Deprecated/`; no legacy source is retired here. Deliver the complete clean repository ZIP with fresh timestamps for all archived files, without generated build/dependency artifacts or cleanup/migration scripts.

Implementation reference: [Slang reflection layouts and entry-point parameters](https://docs.shader-slang.org/en/latest/external/slang/docs/user-guide/09-reflection.html). The CLI sidecar behavior above is verified against the repository's pinned Slang `2026.16.1` compiler.

### RHI fixed descriptor-array checkpoint — 2026-09-09

This checkpoint connects Slang fixed resource arrays to the existing Vulkan descriptor-array allocation/write path, using `Swim-Engine(20260909-001811).zip` as the baseline. Global and direct entry-point declarations use the same conversion. Item **39** remains open for desktop validation before **40** (RenderGraph).

- [x] Preserve one-dimensional fixed descriptor-array metadata in `ShaderBindingReflection`. Keep `TypeKind == "array"`, retain `DescriptorElementTypeKind` and `DescriptorArrayCount`, and parse the resource shape, access, numeric class and explicit storage-image qualifier from the element contract. Append fields to preserve existing aggregate positions. Arrays of values inside a buffer remain ordinary buffer data.
- [x] Derive `DescriptorBindingDesc::Count` from the array type's positive `elementCount`. The pinned SPIR-V sidecar's binding-slot count is absent or one; it is not the number of descriptors. One array occupies one binding, so another array may use the immediately following binding number. Reject malformed/conflicting slot counts instead of multiplying indices or counts.
- [x] Support fixed arrays of samplers, uniform buffers, read-only storage buffers, sampled 2D float/normalized textures, compute storage buffers and typed 2D storage images. Preserve global/entry stage visibility, exact storage-image format validation and duplicate-slot rejection. Conversion failure discards the complete output interface.
- [x] Reject absent, zero, negative, nonintegral, string/unbounded and overflowing array lengths, missing element types, nested descriptor arrays, arrays of value structs/scalars or parameter blocks, and existing unsupported image shapes/access. A maximum `uint32_t` count is representable as metadata; native limits reject excessive allocations before descriptor storage is allocated.
- [x] Split binding/type parsing into `ShaderBindingReflection.h/.cpp` and shared JSON reads into `ShaderReflectionJson.h`. `ShaderReflection.cpp` retains document/entry traversal and stage handling. JSON types remain tool-side; no runtime dependency or engine subproject is added.
- [x] Verify native layout/pool element counts, combined descriptor limits, out-of-order writes, repeated writes that leave holes, incomplete-table bind rejection, atomic failed batches, per-element storage-image format checks, immutable recorded tables and replacement tables. The runtime already supported these rules; this checkpoint supplies reflection and direct regression coverage rather than introducing a second descriptor system.
- [x] Add pinned `DescriptorArrays.slang` and opt-in `RHI.Vulkan.Smoke.FixedDescriptorArraysAndReadback`. Six bindings across two spaces each contain two descriptors. The shader uses every array element, including entry-local writable buffers, sampled textures/samplers and typed storage images. Four frames/two frame slots exercise changing buffer/uniform data, guard elements, two dependent passes, partial push updates and buffer/image readback. Each table is populated in reverse element order.
- [x] Run all **sixteen** native smokes on Windows/Linux desktops with the documented validation profiles, retaining adapter/driver and post-teardown diagnostics. Test both dedicated-compute and graphics-aliased compute queues where available. *(Done: all nineteen item 39 smokes pass the four validation profiles on Windows and Linux; see [the item 39 record](validation/Item39-2026-09-12.md).)*

**Declaration and writes:** `[[vk::binding(2, 0)]] StructuredBuffer<uint> Inputs[2];` produces binding 2 with descriptor count 2, not bindings 2 and 3. Write each resource through `DescriptorWrite::ArrayIndex` (0 and 1), then bind the complete table. Every element must be initialized, even if a shader branch does not access it. No writes are allowed after recording a bind; allocate a replacement table and retain the old one through GPU completion. Existing buffer/image state transitions and resource lifetime rules still apply.

**Indexing policy:** this checkpoint's portable consumer uses compile-time constant descriptor indices (`Inputs[0]`, `Inputs[1]`); indexing the contents of each buffer with a varying invocation index is independent. Fixed allocation does not enable dynamic or non-uniform descriptor indexing for every resource class. Such shaders require their specific Vulkan features and compatible SPIR-V; the existing sampled-image indexing baseline is not a blanket capability for uniform buffers, storage buffers or storage images. The converter does not inspect shader indexing expressions or validate arbitrary SPIR-V capabilities. No device-feature baseline is broadened here.

**Scope:** runtime-sized arrays, variable descriptor counts, partially bound arrays, update-after-bind mutation, bindless allocation/retirement policy and multidimensional descriptor-array flattening remain separate work. An array of `Texture2D` descriptors is also distinct from a single `Texture2DArray` image; the existing image-view shape restrictions remain. Implicit uniform containers, nested parameter blocks, automatic vertex-interface validation and broader sampled-image formats/shapes remain open.

**Validation:** A clean Linux Debug configure/build passed with GCC 13.3, CMake 4.4.3, Ninja 1.13.2 and pinned Slang `2026.16.1`; the asset compiler, Jolt backend and legacy engine were disabled, and cached dependencies were reused. All **324 default cases / 3,911 checks** passed (including seven new reflection cases and four new Vulkan capture cases). All nine public-header/backend-contract compile targets and `scripts/verify-build-layout.py` passed. The compiled array sidecar/SPIR-V is exercised by the reflection/native-layout regressions. The workspace-generated executable had a damaged ELF header; relinking the same objects and libraries to temporary local storage produced the executable used for these runs, without source or build-system workarounds. An opt-in run with `SWIM_RUN_RHI_SMOKE=1 SWIM_RHI_VALIDATION=all` registered **16 cases**, all blocked at SDL initialization (`No available video device`); no GPU dispatch/readback, native validation-layer success, dedicated-compute behavior or Windows/MSVC build is claimed. Item **39** remains open. The complete clean source ZIP preserves all baseline source files, excludes generated caches/bytecode, and refreshes every entry timestamp to prevent stale incremental objects when extracting over an existing checkout.

**Following work:** complete item **39** desktop validation before **40**. Sampled Float/Uint/Sint classes are implemented in the following checkpoint. Remaining independent resource tasks include broader sampled-image dimensions/depth comparison, nested layouts, and the runtime-sized/bindless policies above. Item **41** retains graph scheduling beyond direct transfers/arenas. Preserve consolidated engine targets and top-level `Deprecated/`; no legacy sources are retired here. Ship the complete clean repository with fresh archive timestamps and no generated build/dependency artifacts or cleanup/migration scripts.

Implementation references: [Khronos descriptor arrays and binding counts](https://docs.vulkan.org/guide/latest/descriptor_arrays.html) and [Slang reflection array/type layouts](https://docs.shader-slang.org/en/latest/external/slang/docs/user-guide/09-reflection.html). Sidecar behavior is checked against the repository's pinned Slang `2026.16.1` compiler.

### RHI sampled-texture numeric-class checkpoint — 2026-09-09

Baseline: `Swim-Engine(20260909-071509).zip`. Preserve the user's Debug-build configuration and Slang `-fvk-use-entrypoint-name` fix. This completes the signed/unsigned sampled 2D color portion of the broader sampled-image contract; image shapes and depth comparison remain separate tasks. It is implemented in the new RHI path, not wired into sandbox rendering. Item **39** remains open before **40**.

- [x] Add backend-neutral `SampledTextureClass` and append `DescriptorBindingDesc::SampledClass`. Float is the existing/default contract; Uint and Sint are explicit. Non-image descriptors retain Float. Program and layout copies own this metadata together with the descriptor schema.
- [x] Convert Slang `Texture2D<float/uint/int>` scalar/vector results (one through four components), including global/direct-entry resources and fixed descriptor arrays. Reject unsupported scalar widths/classes, malformed result widths and existing unsupported image/access shapes. Preserve stage visibility, binding coordinates, collision rejection and whole-interface failure behavior.
- [x] Add focused `RhiSampledTexture.h` format classification: floating/normalized/sRGB/compressed color formats map to Float, unsigned integer formats to Uint, signed integer formats to Sint. Depth/stencil, Undefined and unknown formats reject. This classification does not promise device support for a format.
- [x] Validate numeric-class contracts before native descriptor-layout creation. Sampled writes require the view's numeric class to match the shader; channel count/bit width need not match (for example `Texture2D<uint>` can read R8Uint or R32Uint). Exact view/texture format and valid mip/layer ranges remain required; current views do not enable mutable-format reinterpretation.
- [x] Require sampled-image format support for integer views without requiring linear filtering. Preserve the existing sampled-plus-linear feature requirement for Float views. Integer texel loads use no sampler. Arbitrary shader/sampler pairing and filter validation are not inferred from reflection; callers using sampling operations remain responsible for compatible sampler/features.
- [x] Exercise signedness mismatch, format-feature rejection, invalid classes, metadata ownership, failed-batch atomicity, reverse-order fixed-array writes, initialization/sealing and nonzero mip/layer ranges through deterministic native-dispatch tests. Include the new public helper in the existing narrow RHI header gate.
- [x] Compile `SampledInteger.slang` with the existing artifact pipeline and add `RHI.Vulkan.Smoke.SampledIntegerTexturesAndReadback`. Four frames/two frame slots read R32Uint, R8Uint, R32Sint and RGBA8Unorm images through mip-1/layer-1 views; global uint arrays and a direct-entry signed texture span two descriptor spaces. Readback checks high unsigned bits, negative signed values, narrow integer widening, normalized conversion and untouched output guards. Descriptor indices are constant and all work stays on one compute-capable family.
- [x] Run all **seventeen** native smokes on Windows/Linux desktops with the documented validation profiles and post-teardown diagnostics. The sandbox executable and default capture tests are separate evidence. *(Done: all nineteen item 39 smokes pass the four validation profiles on Windows and Linux; see [the item 39 record](validation/Item39-2026-09-12.md).)*

**Usage:** a reflected `Texture2D<uint>` binding has `SampledClass == Uint`; populate it with a Uint color view and use `Load(int3(x, y, mip))` for unfiltered integer reads. The mip argument is relative to the view. Signed reads retain signed values; reinterpreting with Slang `asuint` preserves their bits. Exact storage-image qualifiers continue to apply only to writable storage textures.

**Validation:** Linux Debug compilation/linking passed with GCC 13.3, CMake 4.4.3, Ninja 1.13.2, cached real dependencies and pinned Slang `2026.16.1`. Asset compiler, concrete physics backends and the Windows-only legacy engine were excluded. After regenerating CMake to discover the three added suite files, all **333 default cases / 4,223 checks** passed, including four new reflection cases and five new Vulkan capture cases; the configured test target includes 91 suite source files. All nine public-header/backend-contract compile gates and `scripts/verify-build-layout.py` passed. The new shader compiled successfully, its JSON reflected `computeMain`, and direct SPIR-V inspection confirmed the same `OpEntryPoint` name. All **17** opt-in smokes were attempted with `SWIM_RUN_RHI_SMOKE=1 SWIM_RHI_VALIDATION=all`; each failed at SDL initialization (`No available video device`) before real GPU work. GPU readback/validation, Windows/MSVC and dedicated-compute execution are not claimed. Item **39** remains open. The input archive's Slang build helper and Windows build scripts were preserved byte-for-byte.

**Following work:** complete item **39** desktop validation before RenderGraph. Sampled-image dimensions are implemented in the following checkpoint; depth comparison, nested resource layouts and runtime-sized/bindless policies remain independent RHI tasks. The completed numeric-class work does not implement shader/sampler use analysis, alternate bit-width shader scalar types or mutable image formats. Preserve consolidated engine targets and top-level `Deprecated/`; no legacy source is retired in this checkpoint. Deliver a full clean repository ZIP with refreshed timestamps, excluding generated outputs and dependency caches.

Implementation references: [Vulkan image component conversion](https://docs.vulkan.org/spec/latest/chapters/images.html), [Vulkan image formats](https://docs.vulkan.org/spec/latest/chapters/formats.html) and [Vulkan sampling](https://docs.vulkan.org/spec/latest/chapters/textures.html). Actual reflection and SPIR-V are produced by the repository's pinned Slang compiler.

### RHI sampled-image dimension checkpoint — 2026-09-09

Baseline: `Swim-Engine(20260909-133222).zip`. This completes the sampled-color view-shape contract following Float/Uint/Sint support. The user's Debug scripts and Slang entry-point-name fix are preserved. This remains a modern RHI consumer; sandbox rendering still uses the transitional Vulkan renderer. Item **39** stays open before **40**.

- [x] Append `DescriptorBindingDesc::SampledDimension`, defaulting to Texture2D for compatibility. Preserve it in owned program/layout schemas, independently of descriptor-array Count and sampled numeric class. Non-sampled descriptors retain the default dimension.
- [x] Convert Slang 1D, 1DArray, 2D, 2DArray, 3D, Cube and CubeArray color resources for Float/Uint/Sint scalar/vector results, globals, direct entry resources and fixed descriptor arrays. Unsupported multisampling, 3D arrays, scalar widths, result widths and access modes still reject without a partial interface.
- [x] Share texture-view shape, exact-format and overflow-safe subresource validation between native view creation and sampled descriptor writes through focused `Internal/VulkanTextureViews.h/.cpp`. Require exact shader/view dimensionality, including one-layer array views. Cube views cover six layers; cube arrays cover multiples of six; 3D views use image layer zero. Existing cube face/2D-array views are valid; 2D views of 3D images remain unsupported because image creation does not request the required compatibility flag.
- [x] Expose `GraphicsCapabilities::SampledCubeArray` and enable Vulkan imageCubeArray only when available. Reject cube-array layouts and native views when it is not enabled. The required adapter baseline is unchanged; plain cubes remain usable without cube-array support.
- [x] Add reflection and native-dispatch regression coverage for all shapes, numeric classes, scopes, descriptor counts, owned layout metadata, format/range mismatches, optional-feature rejection, native view types/subresources, one-layer array semantics and failed-batch atomicity.
- [x] Compile base and optional cube-array variants of `SampledDimensions.slang` through the existing Slang artifact pipeline. Add `RHI.Vulkan.Smoke.SampledDimensionsAndReadback`: four frames/two slots, mip-1 and nonzero base-layer views, integer 1D/array/2D/volume loads, floating cube sampling, all six faces, two optional cubes, CPU readback and untouched guards. A direct-entry array texture exercises scoped reflection. All copies, barriers and dispatches stay on one compute-capable family and drain before CPU reuse.
- [x] Run all **eighteen** native smokes on Windows/Linux desktops with the documented validation profiles and post-teardown diagnostics. Cube-array coverage is reported explicitly when supported; the base shapes remain required otherwise. *(Done: all nineteen item 39 smokes pass the four validation profiles on Windows and Linux; see [the item 39 record](validation/Item39-2026-09-12.md).)*

**Usage:** the image's `TextureDimension` describes allocation geometry; `TextureViewDimension` describes the shader-visible view. `Texture2DArray<uint> Images[3]` requires three descriptor elements, each a Uint 2D-array view; its image layers do not change descriptor count. Sampling coordinates and mip indices are relative to the selected view. Cube layer groups use Vulkan face order (+X, -X, +Y, -Y, +Z, -Z). Floating views retain sampled-plus-linear format support; integer views require sampled support without linear filtering. Callers remain responsible for compatible sampler operations/features.

**Scope:** this checkpoint does not implement depth/comparison sampling, multisampled shader image types, mutable-format reinterpretation, alternate shader scalar widths, nested layouts, runtime-sized/bindless descriptor policies or queue-family ownership transfer. Storage images retain the separate typed single-sampled 2D contract. Existing image allocation/format support behavior remains; accepting reflected dimensions is not a promise that every format/usage combination is supported on every device.

**Pinned compiler note:** Slang 2026.16.1 emits the Cube-array image type but omits SPIR-V SampledCubeArray capability 45. The optional smoke variant explicitly declares it with Slang inline `spirv_asm`; no bytecode patcher or build-script workaround is added. A regression inspects both compiled modules for their entry-point name, Sampled1D capability, cube-array type and conditional capability declaration. New cube-array shader consumers using this pinned compiler need the same declaration. See [Slang inline SPIR-V](https://shader-slang.org/slang/user-guide/a1-04-interop.html) and the [SPIR-V capability rules](https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html).

**Validation:** Linux Debug compilation/linking passed with GCC 13.3, CMake 4.4.3, Ninja 1.13.2, cached real dependencies and pinned Slang 2026.16.1. Asset compiler, concrete physics backends and the Windows-only legacy engine were excluded. The configured target includes 94 suite source files; all **343 default cases / 5,159 checks** passed (ten added cases: three reflection/artifact and seven native-dispatch cases). All nine public-header/backend-contract compile gates and `scripts/verify-build-layout.py` passed. Both shader variants compiled with `computeMain` preserved; the optional variant's explicit SampledCubeArray capability is covered by the artifact regression. All **18** opt-in smokes were attempted with `SWIM_RUN_RHI_SMOKE=1 SWIM_RHI_VALIDATION=all` and stopped at SDL initialization (`No available video device`), before GPU execution. Desktop GPU readback/validation, dedicated-compute hardware and Windows/MSVC are not claimed; item **39** remains open. The user's Slang build helper and Windows build scripts are preserved byte-for-byte. The complete ZIP retains all original clean repository files, excludes generated outputs, and refreshes every file timestamp.

**Following work:** close item **39** with desktop evidence before RenderGraph. Depth/comparison sampling is implemented in the following checkpoint; nested layouts and runtime-sized/bindless policies remain open. Preserve consolidated engine targets and top-level `Deprecated/`. No legacy source is retired by this checkpoint. Deliver the complete clean repository with refreshed ZIP timestamps and no generated dependency/build output or migration scripts.

Implementation reference: [Khronos image view shape, format and subresource requirements](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageViewCreateInfo.html). Reflection and both shader variants use pinned Slang `2026.16.1`.

### RHI depth/comparison sampling checkpoint — 2026-09-09

Baseline: `Swim-Engine(20260909-223238).zip`. This completes the next independent sampled-depth contract after sampled image dimensions. Preserve the user's changes and consolidated engine targets. The sandbox remains on the transitional Vulkan renderer; item **39** desktop validation is still open before **40**.

- [x] Append backend-neutral `TextureViewDesc::Aspect` with Automatic/Color/Depth/Stencil selection. Automatic preserves existing full-format views. Share aspect selection between native creation and descriptor validation; reject incompatible/unknown selections before creating a native view. Combined depth/stencil sampling requires Depth; depth-only formats also accept Automatic. Stencil sampling remains unsupported.
- [x] Accept depth-only sampled views for Float shader bindings with matching view dimensions and one sample. Keep exact-format, subresource, device ownership and whole-batch validation. Depth descriptors require native sampled-image and depth-comparison support through Vulkan 1.3 format-properties queries; nearest depth reads do not require the color path's linear-filter feature bit. The color numeric classifier remains color-only; depth aspects use an explicit Float descriptor contract.
- [x] Add focused `Internal/VulkanDepthSampling.h/.cpp` checks before VMA allocation for sampled depth: supported 2D/cube allocation geometry, single sampling, legal mip count, native format features and full usage-combination/extent/mip/layer support. D16, D32, D24S8 and D32S8 are recognized subject to those device checks. This is not a blanket format-support promise.
- [x] Enable `SamplerDesc::EnableComparison` and translate all eight comparison operators. Preserve filter/address/LOD validation, allocation limits and resource lifetime behavior. Invalid comparison enumerators reject before native sampler allocation; anisotropy remains outside the current sampler contract.
- [x] Derive sampled descriptor layouts from the same state helper used by image barriers. Depth attachments use DepthStencilReadOnly; sampled-only depth uses ShaderReadOnly because the former layout requires attachment creation usage. Combined depth/stencil barriers continue to transition both aspects without enabling separate layouts. Attachment views must retain all format aspects, preventing a depth-only sampling view from accidentally being used for both native depth and stencil attachments.
- [x] Compile `DepthSampling.slang` and exercise its reflected Float images, comparison-sampler array, ordinary sampler and compute output. Pinned Slang 2026.16.1 JSON reports both sampler types identically; no comparison metadata or pairing analysis is invented. The application configures the sampler to match SampleCmp versus ordinary sampling.
- [x] Add four focused native cases and one compiled-reflection case covering aspect creation, depth formats, descriptor/barrier layout agreement, attachment separation, comparison operators, allocation limits/features, stencil/class rejection and atomic failed batches. Reuse existing capture fixtures; avoid repeating the previous shape/class matrix.
- [x] Add `RHI.Vulkan.Smoke.DepthSamplingAndComparisonReadback`: three frames/two slots, mip-1/layer-1 D32 and packed depth/stencil attachment clears, raw Load and ordinary SampleLevel, LessEqual/Greater SampleCmpLevelZero operations, CPU readback and untouched guards. Select D24S8 or D32S8 using creation preflight. Rendering, compute and readback stay on one graphics/compute-capable family.
- [x] Run the new depth smoke and all **nineteen** native smokes on supported Windows/Linux desktops with the documented validation profiles. Passing default capture tests or running the sandbox does not close this gate. *(Done: all nineteen item 39 smokes pass the four validation profiles on Windows and Linux; see [the item 39 record](validation/Item39-2026-09-12.md).)*

**Usage and limits:** create a full-aspect attachment view and a Depth sampling view for a combined depth/stencil image. Transition DepthStencilWrite to ShaderRead before sampling and transition back before the next attachment pass. `Texture2D<float>` can read color or depth; its source declaration does not identify a depth format. Use an ordinary sampler for ordinary sampling and enable comparison for SampleCmp; reference comparison is against the fetched depth before filtering. Nearest sampling is the validated path. No sampler/image use analysis, automatic shadow-map policy, PCF policy, multisample shader images, stencil reads, depth/stencil buffer copies, independent aspect transitions or reverse-Z convention changes are introduced. Depth 1D/3D allocations remain unsupported. Cube-array feature and pinned Slang capability requirements from the previous checkpoint still apply.

**Validation:** Linux Debug compilation/linking passed with GCC 13.3, CMake 4.4.3, Ninja 1.13.2, cached real dependencies and pinned Slang 2026.16.1. Asset compiler, concrete physics backends and the Windows-only legacy engine were excluded. All **348 default cases** have passing evidence: the full run passed 347 and exposed a new fixture re-recording a one-time command list; after correcting that fixture, the focused depth suite passed all four cases / 77 checks. Production code did not change after the full run. The configured target includes 97 suite source files. Both affected `SwimRhiPublicHeaders` and `SwimRhiVulkanPublicHeaders` gates and `scripts/verify-build-layout.py` passed; unrelated header gates and older native smokes were not repeated. The new Slang artifact compiled and its reflection test passed. The new opt-in depth smoke was attempted with `SWIM_RUN_RHI_SMOKE=1 SWIM_RHI_VALIDATION=all`, but SDL reported `No available video device` before GPU execution. Desktop readback/validation and Windows/MSVC are not claimed. Item **39** remains open. The full clean ZIP preserves original build scripts and Slang helpers, excludes generated outputs and refreshes all file timestamps.

**Following work:** close **39** with real desktop evidence before **40**. Nested resource layouts/explicit parameter blocks are implemented in the September 12 checkpoint below; runtime-sized/bindless policies and automatic vertex-interface validation remain open. No legacy technology is retired by this checkpoint. Keep top-level `Deprecated/`, preserve build scripts, and deliver a full clean repository ZIP with fresh file timestamps and no generated caches or migration scripts.

Implementation references: [Vulkan sampler comparison state](https://docs.vulkan.org/refpages/latest/refpages/source/VkSamplerCreateInfo.html), [image-view aspect requirements](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageViewCreateInfo.html), [depth-comparison format support](https://docs.vulkan.org/refpages/latest/refpages/source/VkFormatFeatureFlagBits2.html) and [image barrier layouts](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageMemoryBarrier2.html). Reflection behavior is checked against the actual pinned compiler.

### RHI nested resource layout / parameter-block checkpoint — 2026-09-12

Baseline: `Swim-Engine(20260912-141918).zip`. This completes the next explicit nested-resource contract identified by the preceding checkpoint. The sandbox remains on the transitional renderer; item **39** desktop validation stays open before **40** (RenderGraph).

- [x] Resolve nested resource structs and explicit `ParameterBlock<T>` / `ConstantBuffer<T>` layouts in global and direct entry-point parameters. Traverse the authoritative `elementVarLayout` rather than the duplicate `elementType` layout. Keep descriptor binding, descriptor set, child-set allocation and uniform byte offsets separate; check arithmetic overflow and cap traversal depth at 64.
- [x] Normalize supported parameter groups to owned descriptor leaves with qualified source names. A resource-only block contributes its resources without a phantom uniform buffer. A group containing uniform data contributes one UniformBuffer descriptor plus its separate resource descriptors. Fixed resource arrays retain their array count at one binding. Typed sampled/storage image validation, stage visibility, duplicate-slot rejection and whole-interface rollback remain shared with the existing converter.
- [x] Retain uniform-buffer `Size` / `HasSize` and owned `UniformFields` (`Name`, `Offset`, `Size`) in tool-side reflection. Byte offsets are relative to that buffer, including nested struct offsets; value arrays/matrices retain their complete reflected range. New `ShaderUniformReflection.h` and the focused layout/offset helpers keep JSON and Slang implementation types outside generic/public headers.
- [x] Reject malformed/duplicate binding categories, invalid counts/coordinates, unsupported nested resource types, unbacked/out-of-buffer uniform ranges, malformed field lists, excessive nesting and arrays of resource-bearing parameter groups. Failed traversal emits one unsupported marker for the original parameter, discarding partially expanded leaves. Malformed/unsupported global scope containers now also reject instead of silently dropping parameters.
- [x] Add compute and graphics Slang examples to the existing shader artifact target. Compute covers explicitly located parent/child sets, uniform buffers, nested resource structs, fixed buffer arrays and typed sampled images. Graphics covers shared nested resources and a fragment-local parameter block. Update the existing Basic sample regression for normalized material leaves and verify its complete RHI interface.
- [x] Validate with focused shader compilation/reflection regressions and an independent comparison against actual SPIR-V DescriptorSet/Binding decorations. No Vulkan backend behavior changes, extra engine subproject or duplicate GPU smoke harness is introduced.
- [x] Execute item **39**'s existing nineteen native smokes on Windows/Linux desktops with the documented validation profiles and teardown diagnostics. This checkpoint does not claim desktop GPU execution or Windows/MSVC validation. *(Done: all nineteen item 39 smokes pass the four validation profiles on Windows and Linux; see [the item 39 record](validation/Item39-2026-09-12.md).)*

**Consumer contract:** `ParseSlangReflectionJson` / `LoadSlangReflectionJson` return normalized descriptor leaves in `GlobalParameters` and `EntryPoints[].Parameters` for supported explicit groups. For example, a resource-only `Material` block now appears as `Material.AlbedoTexture` and `Material.AlbedoSampler`; it does not retain a non-bindable `Material` parent record. A group with uniform bytes retains its group name for the generated buffer descriptor, and its `UniformFields` identify the byte ranges to populate. Feed the parsed reflection to `BuildRhiShaderInterface`, create/write the returned descriptor spaces through the existing RHI, and retain resources through submission completion. Global leaves use all program stages; entry leaves use their declaring stage. This is a tool metadata normalization change, not a runtime RHI ABI change.

**Example:** `NestedParameters.slang` explicitly places `Settings` in set 2. Set 2 contains binding 0 (16-byte uniform buffer: Count at byte 0, Scale at byte 4), binding 1 (two input-buffer descriptors), and binding 2 (one Uint sampled image). `Settings.Child` occupies set 3 with the same descriptor shape and its Bias at byte 0 of a separate 16-byte buffer. Output remains set 5, binding 0. These coordinates are checked against the compiler's SPIR-V, independently of JSON traversal.

**Limits:** this implements explicit nested resource layouts, not an automatic CPU packing/serialization API. Uniform field ranges do not describe matrix major order, array stride or scalar conversion; callers must supply bytes matching the compiled shader layout. RHI buffer-range enforcement is unchanged. Implicit global/entry uniform scope containers and entry-local push blocks remain rejected; the supported global push-constant contract is unchanged. Arrays of parameter blocks/resource-bearing structs, runtime-sized descriptors, variable counts, partial binding, nonuniform indexing capabilities, update-after-bind and timeline-safe bindless allocation remain separate work. Array-valued data inside a uniform/structured buffer is distinct from a descriptor array.

**Validation:** GCC 13.3 / C++20 compiled the shader compiler and the existing single-runner shader suites directly against cached simdjson 3.12.3. All **53 shader cases / 2,276 checks** pass, including ten new nested-layout cases and all enabled compiled-artifact regressions. All **16** configured sample/RHI Slang programs compile with pinned Slang **2026.16.1**, column-major matrix layout, preserved parameters and preserved entry-point names. The narrow shader public-header compile gate and `scripts/verify-build-layout.py` pass. The only new compiler warning (a copied range-loop string in a test) was corrected and checked by focused recompilation. This was focused tool-side validation: CMake is unavailable in this workspace, so no full CMake engine build, unrelated runtime suite, GPU execution or Windows/MSVC build is claimed. Existing backend tests were not duplicated or rerun.

**Following work:** close **39** with real Windows/Linux desktop evidence before starting **40**. Runtime-sized/bindless policies and automatic vertex-interface validation remain independent shader/RHI work. Implicit scope uniform packing remains explicitly unsupported. Preserve the consolidated engine targets, original build scripts and top-level `Deprecated/`. Per this request, deliver a ZIP containing **only changed/added files**, with repository-relative paths and fresh timestamps; no generated outputs or dependency caches.

### RHI item 39 desktop validation checkpoint — 2026-09-12

The loader lifetime, sampled SPIR-V image-format, GPU validation setup and SDK-selection defects are fixed. All nineteen native smokes pass the four documented profiles on Windows and Linux. Windows strict HDR also passes. The Linux run uses Mesa llvmpipe and an Xvfb/Openbox desktop; it is actual Vulkan execution with software rendering. The approved descriptor-limit advisory is retained and narrowly accepted; no other warnings, errors, dropped diagnostics or lifecycle assertions are waived. The default suites pass **422 cases / 10,086 checks** on Windows and **363 cases / 5,878 checks** in the Linux foundation/RHI/shader configuration. Public-header gates and architecture verification pass. Full scope, toolchain versions, commands and raw-report locations are in [the validation record](validation/Item39-2026-09-12.md). Earlier checkpoints retain their historical status.

### Phase 9 exit criteria

- [x] clear/triangle/texture test on Windows and Linux. *(Windows hardware; Linux Mesa software Vulkan; full nineteen-case matrix recorded above.)*
- [x] validation clean under the documented acceptance policy. *(Only the exact approved GPU-AV descriptor-limit startup advisory is accepted and retained.)*
- [x] VMA used for normal buffer/image allocation.
- [x] resize/minimize/restore loop is stable on Windows and Linux. *(Native lifecycle cases pass on Windows and Linux Xvfb/Openbox with the original assertions and deadlines.)*
- [x] frame lifetime uses timeline-based retirement. *(The backend-neutral frame ring waits reused contexts by timeline value and retains retired RHI objects until completion.)*

---

## Phase 10 — Render graph and frame scheduler

Build this before porting lots of renderer features.

### Render graph contract

```cpp
GraphTexture Depth = Graph.CreateTexture(DepthDesc);
GraphBuffer Visible = Graph.CreateBuffer(VisibleDesc);

Graph.AddPass("CullInstances",
    [&](RenderGraphBuilder& Builder)
    {
        Builder.Read(GpuSceneBuffer);
        Builder.Read(PreviousHzb);
        Builder.Write(Visible);
    },
    [&](RenderCommandContext& Context)
    {
        Context.Dispatch(...);
    });
```

### Responsibilities

- [x] pass dependency DAG;
- [x] read/write declarations;
- [x] read-before-write validation;
- [x] cycle detection;
- [x] resource lifetime analysis;
- [x] image layout/resource-state transitions;
- [x] memory barriers;
- [ ] queue ownership transfer; *(Deferred with dedicated-queue scheduling. The initial executor keeps all pass roles on the graphics family and rejects imports declaring another owner.)*
- [x] pass culling;
- [x] imported persistent resources;
- [x] exported resources;
- [x] transient resource pooling;
- [x] graphics/compute/transfer passes;
- [x] GPU labels/timestamps per pass;
- [x] deterministic ordering where dependencies are otherwise equal.

### Async compute

Do not force async-compute complexity immediately.

First:

1. make dependencies correct;
2. profile pass overlap opportunities;
3. schedule async only when it produces measurable value.

### Item 40 implementation checkpoint — 2026-09-12

`Swim::Render` now provides the backend-neutral graph definition, compiler, checked pass context and timeline-owned executor under `Systems/Renderer/RenderGraph`. Graphics, compute and transfer pass roles share one graphics queue. The compiler validates initialization and state/usage/range declarations, detects cycles, distinguishes data dependencies from ordering-only hazards for culling, and synthesizes same-state write barriers as well as texture layout changes. Compatible transient objects share slots only across nonoverlapping lifetimes; exports remain pinned, imports are never pooled, and the executor waits before reusing resources or query slots. External WSI semaphore/timeline dependencies are forwarded explicitly.

All 17 graph CPU cases pass on Windows and Linux. The complete Windows default suite passes 439 cases / 10,185 checks; the available Linux default suite passes 380 / 5,977. Windows passes all 21 native Vulkan smokes under core, synchronization, GPU-assisted and combined profiles. Linux passes both new graph rendering/compute smokes under all four profiles on llvmpipe with Xvfb/Openbox. Native tests verify exact pixel/compute readback, repeated pooling, per-pass timestamps, presentation and swapchain replacement. The exact previously approved GPU-AV startup advisory is the sole accepted warning. See [contracts and reproduction](RenderGraph.md) and [the retained evidence summary](validation/Item40-2026-09-12.md).

Item **40** is complete for the DAG/resource-state/barrier foundation. Phase 10's dedicated-family ownership transfer and async scheduling are deliberately still open; there is no claim of overlapping queues/frames or Linux physical-GPU coverage. The sandbox is still transitional, and no legacy source is retired here. Item **41** remains next: connect the existing upload/readback arenas and transfer helpers to graph-scheduled ownership and completion. Keep the consolidated engine/test source layout and top-level `Deprecated/`.

### Item 41 graph-scheduled transfer checkpoint — 2026-09-22

Item 41's remaining work — connecting the upload/readback arenas and transfer helpers to graph-scheduled ownership and completion — is implemented. Full contract: [RenderGraph staging](RenderGraph.md#staged-transfers-item-41).

- [x] `RenderGraph::CreateUpload(desc, writer)` / `CreateUpload(bytes, name)` declare host-written, GPU-read-only buffers initialized at graph start; `CreateReadback(desc)` declares GPU-written buffers implicitly exported to `HostRead`. Staged buffers cannot be exported, uploads cannot be written by passes, and neither is pooled or aliased.
- [x] `RenderGraphExecutor` owns one `Rhi::UploadArena` and one `Rhi::ReadbackArena`. After the predecessor wait it resets both, suballocates every live staged buffer, then runs upload writers before recording. Used upload bytes are flushed before submission; the readback batch is validated before and committed after the queue call through the new backend-neutral `Rhi::ReadbackSubmission` hook, using the executor's (now shared) completion timeline. Growth happens only between submissions (next power of two, minimum 64 KiB, sized by a conservative alignment bound); there is no in-flight growth, spill or ring wrap. `RenderGraphExecutorDesc` pre-reserves capacity; `Trim` releases the arenas.
- [x] `RenderCommandContext::GetRange` exposes `{Buffer, Offset, Size}` for any declared buffer; `Get` rejects staged suballocations. `TryReadback`/`TryGetReadback` are nonblocking and valid until the next `Execute`/`Trim`.
- [x] `ReadWrite` + `CopyDestination` is now a preserving partial copy (requires initialized contents). `RenderGraphTransfers.h` provides `AddBufferUpload` (span or writer), `AddTextureUpload`, `AddBufferReadback`, `AddTextureReadback` and `GetTextureCopyBytes`, choosing `Write` for whole-resource copies and `ReadWrite` otherwise. The backend-neutral `Rhi::GetUncompressedColorTexelBytes` replaces the Vulkan-private texel table.
- [x] Failures publish nothing: writer/allocation failures leave before recording; flush/validation/submission failures leave the completion value unchanged and the next execution resets uncommitted staging.
- [x] Seven CPU cases (`RenderGraph.Transfers.*`) with byte-exact round trips through the shared mock command stream, plus the opt-in native `RHI.Vulkan.Smoke.RenderGraphStagedTransfersAndReadback`.
- [x] Execute the new native smoke under the four validation profiles on Windows hardware. *(2026-09-22: all 23 smokes pass `core`/`sync`/`gpu`/`all` on the RTX 4070.)*
- [ ] Execute it on Linux llvmpipe.
- [ ] Dedicated transfer-queue ownership and asynchronous upload scheduling (the open queue-ownership responsibility above).

### Phase 10 exit criteria

- [x] an offscreen pass -> post pass -> present sequence uses graph-generated synchronization. *(Native Windows hardware and Linux software-Vulkan tests, including readback and swapchain replacement.)*
- [x] hand-written barriers are no longer scattered through high-level renderer features. *(The new modern graph reference consumers declare usage and contain no manual barriers; transitional sandbox migration and direct-RHI test coverage remain separate.)*
- [x] graph debug dump/view exists. *(Deterministic text dump includes pass order/culling, dependencies, allocation slots, lifetimes and subresource barriers.)*

---

## Phase 11 — GPU resource registries and asset residency

Now connect the already-designed asset system to the RHI.

### GPU handle model

Use compact generational handles for persistent renderer resources:

- `GpuMeshHandle`
- `GpuTextureHandle`
- `GpuMaterialHandle`
- `RenderObjectHandle`
- `GpuSkinHandle`

Do not use shared owning pointers as IDs. *(Implemented in `Renderer/Resources/GpuHandle.h`, plus `GpuSamplerHandle` so image and sampler identity stay separate; see the item 42–43 checkpoint below.)*

### GeometryHeap

Evolve the useful mega-buffer concept into paged/arena residency:

```text
GeometryHeap
  |-- Vertex pages
  |-- Index pages
  |-- Meshlet pages
  |-- Mesh metadata buffer
  |-- allocation table
  |-- free ranges
  `-- timeline retirement queue
```

Requirements:

- [x] large device-local pages rather than one fragile fixed buffer; *(dedicated pages for oversized streams)*
- [x] variable range allocation; *(best-fit `GeometryRangeAllocator`, any alignment, coalescing)*
- [x] stable metadata indirection; *(`GpuMeshMetadata` row = `GpuMeshHandle::Index`, reused only after retirement)*
- [x] 16/32-bit indices;
- [x] multiple packed vertex formats; *(per-mesh stride + caller layout id; whole-vertex offsets)*
- [x] LOD ranges; *(up to eight absolute index ranges with error metric per row)*
- [x] meshlet ranges; *(opaque meshlet payload pages; meshlet generation itself is later work)*
- [ ] async transfer upload; *(uploads are graph-scheduled and staged, but run on the graph's graphics queue until dedicated-queue ownership lands)*
- [x] safe deferred free;
- [x] fragmentation metrics;
- [ ] optional relocation/compaction later.

### Texture residency

- [x] bindless texture table; *(`BindlessResourceTable`, item 45: one update-after-bind table shared by every pipeline that defines its space)*
- [x] separate image identity from sampler identity; *(`BindlessTextureHandle`/`BindlessSamplerHandle` index spaces; `GpuSamplerCache` deduplicates samplers by description)*
- [x] fallback texture; *(permanent element 0 of each array, also written over retired elements)*
- [x] timeline-retired bindless IDs; *(released elements keep their resource until the timeline point, are then rewritten to the fallback and reused oldest-first)*
- [x] residency state separate from AssetHandle validity; *(`TextureResidency`/`AssetResidencyService`; CPU assets are unloaded after staging by default)*
- [ ] KTX2/native compressed upload; *(uncompressed native mip chains upload today; compressed variants are rejected explicitly)*
- [ ] future mip streaming metadata.

### No hidden upload

Asset load and GPU residency are explicit asynchronous state transitions *(implemented by `AssetResidencyService` as `AssetResidencyState`; "Decoding/Decompressing" is the job-side `DecodeSasset` step)*:

```text
Unloaded
 -> Queued
 -> Reading
 -> Decoding/Decompressing if needed
 -> WaitingForGpuUpload
 -> Uploading
 -> Resident
```

### Item 42–43 GPU registry and GeometryHeap checkpoint — 2026-09-22

Full contract and caller lifecycle: [GPU resource residency](GpuResidency.md).

- [x] **Item 42:** `GpuHandle<Tag>` (32-bit index + generation, packable) and `GpuResourceRegistry<Tag, Record>`: immediate handle invalidation, records retired only after their timeline point, FIFO reuse of retired slots, capacity that counts retiring slots, nonblocking `CollectRetired` with a retirement callback, `Drain`, permanent retirement instead of generation wrap. Six CPU cases.
- [x] **Item 43:** `GeometryHeap` with vertex/index/meshlet pages, `GeometryRangeAllocator`, stable `GpuMeshMetadata` rows, per-page batched uploads recorded into a `RenderGraph` (preserving partial copies, graph-generated barriers, pages imported in resting read states), explicit residency with commit/abort, deferred frees with upload-aware retirement points, dedicated-page release, limits with full rollback and fragmentation statistics. Nine CPU cases (two allocator, seven heap) plus the opt-in native `RHI.Vulkan.Smoke.GeometryHeapPagedUploadAndRetirement`.
- [x] Architecture verifier: `Resources/` and `Geometry/` may not include backend, platform, EnTT or scene headers, RenderGraph may not include them, and their suites must be collected. `SwimRenderResourcesPublicHeaders` compiles the public headers alone.
- [x] Execute the native GeometryHeap smoke on Windows under the validation profiles. *(2026-09-22, all four profiles.)* Linux llvmpipe remains.
- [x] Item 44: feed compiled `MeshAsset` data through async IO into `GeometryHeap` (see the item 44 checkpoint below). Retiring `MeshPool`/`VulkanIndexDraw` geometry ownership still waits for the modern renderer to draw from the heap.

### Item 44 asynchronous asset residency checkpoint — 2026-09-22

Full contract: [GPU resource residency](GpuResidency.md#compiled-assets-to-gpu-residency-item-44).

- [x] `Assets::DecodeSasset` validates and decodes mesh/texture/sampler/material-template objects without an `AssetSystem` (job-safe); `Assets::PublishSasset` binds and publishes on the owner thread. Handle-resolving material instances and models stay on `LoadSasset`.
- [x] `BuildMeshGeometryPayload` converts `MeshAsset` streams/attributes/primitives/LODs/meshlets into one packed-stride GeometryHeap payload with a stable layout id and a `GpuMeshletPayloadHeader`.
- [x] `GeometryHeap` submeshes: `GpuSubmeshRecord` draw ranges (absolute first index/vertex offset, material slot) in a row buffer with range allocation, deferred frees and batched row uploads; LODs are submesh ranges. `MaxSubmeshes` joins `GeometryHeapDesc`.
- [x] `TextureResidency`: RHI texture + full-chain view at creation, tightly repacked mips, one graph pass per texture writing every mip and exporting `ShaderRead`, commit/abort/collect/destroy/drain with timeline-retired registry records, uncompressed native-mip 2D payload selection and explicit rejection of everything else.
- [x] `AssetResidencyService`: the Phase 11 state machine with bounded reads and decodes, polled IO (no callback captures), job decode, id/type verification, per-update upload byte budget in request order, capacity backpressure, `RetainCpuAssets` policy, graph import/commit/abort, cancellation/abandonment on release, `AssetError` mapping and statistics.
- [x] Fix the `AsyncIoService` request/job reference cycle (every async read leaked its state and bytes) and release dispatched completion callbacks; regression case `IO.AsyncIoService.DispatchedCompletionsReleaseTheirCaptures`, plus a full-suite LeakSanitizer run.
- [x] Tests: 17 always-built CPU cases (`Render.MeshGeometryPayload`, `Render.TextureResidency`, `Render.AssetResidency`) plus 3 cases in the asset-compiler group that cook real `.sasset` objects and stream them end to end; the opt-in native `TextureResidencyMipChainUploadAndRetirement` smoke.
- [x] Execute the new native smoke on the Windows desktop under all validation profiles. *(2026-09-24: the full native suite passes `core`, `sync`, `gpu` and `all` on the Windows RTX 4070 Laptop.)*
- [ ] Execute it on a Linux desktop.
- [ ] Block-compressed/KTX2 texture upload (block-aware RHI copies and/or runtime transcoding), sampler residency, texture mip streaming.
- [ ] Request `ModelAsset` graphs through the service; construct the service in the engine runtime.
- [ ] Resident-memory budgets and eviction (the current budget limits bytes staged per update).

### Item 45 bindless texture/sampler table checkpoint — 2026-09-22

Full contract: [GPU resource residency](GpuResidency.md#bindless-textures-and-samplers-item-45).

- [x] Shader reflection: unbounded sampler/sampled-texture arrays (`elementCount` 0) convert to Count 0 bindings; other runtime-sized classes still reject.
- [x] RHI contract: `PipelineLayoutDesc::DescriptorSpaces` (explicit spaces replacing reflected ones after a compatibility check, allowed extra stage visibility, required for runtime-sized arrays), `DescriptorBindingDesc::UpdateAfterBind`, `GraphicsCapabilities::BindlessDescriptors`, partially bound completeness, post-bind writes limited to update-after-bind bindings, and binding across identically defined spaces.
- [x] Vulkan: optional `descriptorBindingUpdateUnusedWhilePending`, binding-flag chains and update-after-bind set layouts/pools, update-after-bind limits counted across the whole pipeline layout, and plain limits counted over plain sets only.
- [x] `BindlessResourceTable`: validated layout space, fallbacks at element 0, immediate registration writes, `GetIndex` fallback for stale handles, timeline-retired release with a fallback rewrite before FIFO reuse, `Collect`/`Drain`, statistics.
- [x] `GpuSamplerCache`: sampler identity by description (names ignored), reference counts, retirement after the latest use reported on one timeline, optional bindless registration.
- [x] `AssetResidencyService`: optional `AssetResidencyDesc::Bindless`; textures become bindless when observed Resident, retry while the table is full, stay Resident on the fallback with an error when the RHI refuses the view, and release their element with the texture.
- [x] Tests: 12 CPU cases (`RHI.Vulkan.Bindless` 3, `Render.Bindless` 4, `Render.SamplerCache` 3, one `Render.AssetResidency` case, one runtime-array `ShaderCompiler.DescriptorArrays` case) and the opt-in native `RHI.Vulkan.Smoke.BindlessTableTimelineSafeReuse`, which samples through nonuniform indices, writes an element while earlier work may be pending, and observes the fallback rewrite and reuse from a shader.
- [x] Execute the native bindless smoke on the Windows desktop under all validation profiles. *(2026-09-24: the full native suite passes `core`, `sync`, `gpu` and `all` on the Windows RTX 4070 Laptop.)*
- [ ] Execute it on a Linux desktop.
- [ ] Standalone (layout-free) bindless tables, storage-buffer/storage-image bindless arrays and variable descriptor counts, when a consumer needs them.
- [ ] GPU material records that carry bindless texture/sampler ids (item 59) and the engine runtime constructing the table.

### Phase 11 exit criteria

- [ ] one compiled model uploads without source importer involvement.
- [x] large geometry uses paged GeometryHeap allocation. *(Heap and native smoke exist; the sandbox's legacy renderer does not use it yet.)*
- [x] texture becomes bindless through an explicit residency operation. *(`AssetResidencyService` registers Resident textures with a `BindlessResourceTable`; native smoke pending desktop execution.)*
- [x] GPU resource destruction is timeline safe. *(For registries, GeometryHeap ranges/pages, graph staging, bindless elements and cached samplers.)*

---

## Phase 12 — Persistent GPU Scene and render extraction

This is the core of the modern renderer.

### GPU Scene is not EnTT

GPU Scene is the persistent render-facing database.

EnTT is one producer of updates.

Other producers may include:

- procedural systems;
- terrain/voxel systems;
- particle systems;
- editor/debug systems;
- external simulation clients.

### Suggested GPU records

| Record | Contents |
| --- | --- |
| `GpuInstance` | world/previous transform reference, bounds, MeshId, material-set ID, object ID, flags, skin ID, LOD bias |
| `GpuMesh` | GeometryHeap ranges, bounds, LOD table, meshlet table, vertex-format metadata |
| `GpuMaterial` | variant/template ID, parameter offset, bindless texture/sampler IDs, flags |
| `GpuLight` | type, transform, color/intensity, range/cone, shadow handle, flags |
| `GpuSkin` | joint matrix range/count/generation |
| `GpuView` | current/previous matrices, frustum, jitter, viewport, exposure/history flags |

### Stable RenderObject identity

A render object is not an `entt::entity`.

Scene extraction maintains a mapping:

```text
(scene, entity, render-subobject)
        -> RenderObjectHandle
```

This allows the renderer to preserve slots even if CPU containers move.

### Extraction

Scene extraction should emit compact commands/dirty ranges:

- create render object;
- destroy render object;
- set mesh;
- set material set;
- set transform;
- set visibility flags;
- set skin;
- update bounds.

Static objects do not rebuild complete CPU draw lists each frame.

### Dirty updates

- [x] upload only changed transforms/materials/lights; *(transforms and instance rows via `GpuRecordBuffer`; materials are a producer-owned material-set id until item 59; lights are Phase 14)*
- [x] batch adjacent ranges; *(sorted dirty rows become contiguous runs, one `CopyBuffer` each, one staging allocation per buffer)*
- [x] retain previous transform for motion vectors; *(first move per frame shifts Current into Previous; objects that stop moving settle once)*
- [x] stable object IDs; *(`RenderObjectHandle` rows; `(scene, entity, part)` keys in `RenderExtractor`)*
- [x] scene unload destroys object handles through deferred GPU-safe retirement. *(`RenderExtractor::ReleaseAll(lastUse)` → `GpuScene::Destroy`: rows dead at the next upload, reusable after `lastUse`)*

### Items 46–48 GPU Scene, extraction and stress checkpoint — 2026-09-22

Full contract: [GPU Scene](GpuScene.md).

- [x] `Renderer/GpuScene`: `GpuInstanceRecord`/`GpuTransformRecord` std430 rows, `RenderAffine`, `RenderBounds`, `RenderObjectFlags` (producer bits plus GpuScene-owned `Live`/`HasMesh`), `RenderObjectDesc`, `ResolvedRenderMesh`.
- [x] `GpuRecordBuffer<T>` + `RecordRowRunsUpload`: persistent device-local record buffer, CPU mirror, deduplicated dirty rows, run batching, staged graph upload, commit/abort.
- [x] `GpuScene`: create/destroy/set API without EnTT, change detection per field (unchanged values mark nothing), previous-transform tracking and settling, timeline-retired row reuse on `GpuResourceRegistry`, exclusive import with commit/abort, statistics.
- [x] Shader side: `Shaders/Slang/GpuScene/GpuSceneRecords.slang` (records, flag constants, drawability and transform helpers). Structured-buffer element layouts are now reflected (`ShaderBindingReflection::ElementFields`/`ElementSize`); `ShaderCompiler.GpuSceneLayout` compares them with the C++ records.
- [x] `Components/MeshRenderer.h` and `Scene/RenderExtraction/RenderExtractor`: signal-driven change sets in deterministic order, `TransformSystem` dirty-list transform updates, part reconciliation with stable rows, mesh resolution retry, capacity retry, `ReleaseAll`/`Rescan`/`RefreshMeshes`. The Transform-backed world source is `RenderExtraction/Runtime/TransformWorldSource` (SwimEngine only).
- [x] `AssetResidencyService::ResolveRenderMesh` keeps each mesh's bounds from staging.
- [x] Item 48 stress: `Render.GpuSceneStress` and `Scene.RenderExtraction.HundredThousandEntitiesExtractDirtyOnly` assert upload rows/bytes per frame for 100k objects with 1% motion; the native `GpuSceneHundredThousandObjectsDirtyUploads` smoke verifies all 100k rows on the GPU over four frames.
- [x] Architecture verifier: GpuScene stays below residency/extraction and free of EnTT/backends; lower layers never include it; the extraction core never includes the Transform/Scene runtime; suites are collected.
- [x] Execute the native smoke on the Windows desktop, and build and pass the EnTT extraction suite on Windows (Linux has no EnTT configuration). *(2026-09-24: the full native suite passes `core`, `sync`, `gpu` and `all` on the Windows RTX 4070 Laptop.)*
- [ ] Execute the native smoke on a Linux desktop.
- [ ] Construct `RenderExtractor` + `GpuScene` in the engine runtime and draw from them (Phase 13 visibility, then item 56 retires the CPU visible list).
- [ ] GPU lights, views and skins as further `GpuRecordBuffer` tables; material sets become item 59 records.

### Phase 12 exit criteria

- [ ] 100k persistent objects render with dirty-only CPU->GPU updates. *(Dirty-only updates for 100k objects are asserted on the CPU and probed on the GPU (item 48); drawing them waits for Phase 13.)*
- [x] renderer can create a RenderObject without EnTT. *(`GpuScene::Create`; EnTT is one producer through `RenderExtractor`.)*
- [ ] no high-level draw loop walks `shared_ptr<MaterialData>` objects.

---

## Phase 13 — Fully GPU-driven visibility and draw generation

This is a first-class architecture goal, not an optional optimization pass.

### Target pipeline

```text
GpuScene + GpuView
      |
      v
instance frustum cull
      |
      v
previous-frame HZB occlusion
      |
      v
LOD selection
      |
      v
optional meshlet/cone cull
      |
      v
visible-list compaction
      |
      v
pass/material/pipeline binning
      |
      v
indirect command/count generation
      |
      v
DrawIndexedIndirectCount / RHI equivalent
```

### Required behavior

- [x] no CPU-visible-list round trip for frame correctness; *(`GpuVisibility` → `DrawIndexedIndirectCount`; proven by the native smoke. The engine runtime still uses the legacy path until item 56.)*
- [x] HZB built each appropriate frame; *(`HzbBuilder` from the early phase's depth, every frame that uses occlusion)*
- [x] conservative behavior for newly visible/teleported objects; *(two-phase: the late test uses this frame's HZB, so revealed objects are drawn in the same frame; objects visible last frame are drawn early regardless)*
- [x] camera cut invalidates occlusion history; *(`GpuViewFlags::CameraCut` resets LOD and occlusion history; reused rows carry a new generation)*
- [x] GPU compaction; *(per-bin atomic slots)*
- [x] GPU draw counts; *(one count per bin)*
- [x] LOD hysteresis; *(per-row `GpuLodState`, `threshold × (1 ± h)`)*
- [x] bounded binning structures; *(`VisibilityBinLayout`: fixed capacity per material bin × index page; overflow counted as `Dropped`)*
- [x] asynchronous diagnostic counters only; *(`VisibilityStats` via the readback arena, never waited on in-frame)*
- [x] indirect-count fast path; *(`Rhi::CommandList::DrawIndexedIndirectCount`)*
- [x] fallback path only for capabilities that genuinely require it. *(`SelectVisibilityDrawPath`: devices without `IndirectCount` zero the command buffer and draw whole bins with `DrawIndexedIndirect`; everything else uses `DrawIndexedIndirectCount`)*

### Relationship to current BVH work

Do not blindly delete useful current work.

Preserve/port ideas such as:

- stable renderable slots;
- persistent world static data;
- dirty transform ranges;
- wide CPU BVH snapshot experiments if they remain useful after profiling;
- indirect-count support.

But the final high-level ownership belongs to GPU Scene/Visibility modules, not one massive Vulkan draw manager.

### Meshlets

Generate meshlets offline now so the data is available.

Use indexed-indirect rendering as the excellent baseline. Mesh/task shader paths remain optional capability-based accelerators and should not block the renderer.

### Items 49, 52–55 and 57 GPU visibility checkpoint — 2026-09-23

Full contract: [GPU visibility](GpuVisibility.md).

- [x] RHI `DrawIndexedIndirect`/`DrawIndexedIndirectCount` + `DrawIndexedIndirectCommand`; Vulkan validation (usage, alignment, stride, range, `maxDrawIndirectCount`, bound index buffer); `multiDrawIndirect` and `drawIndirectFirstInstance` enabled.
- [x] `GpuViewRecord`/`RenderViewDesc`/`BuildGpuViewRecord`: row-major view-projection, depth-[0,1] frustum planes, LOD parameters, view flags.
- [x] `VisibilityMath` + `RunVisibilityReference`: the CPU definition (drawability, sphere frustum test, projected-error LOD, hysteresis, material/page binning, command generation, statistics).
- [x] `GpuVisibility.slang` + `GeometryRecords.slang`/`VisibilityRecords.slang`; `ShaderCompiler.GpuSceneLayout.VisibilityProgramMatchesItsCppContract` checks the layouts.
- [x] `GpuVisibility`: persistent material-bin table and LOD history, clear/cull/readback passes, bounded bins, transient indirect command/count buffers.
- [x] Item 57: `VisibilityStats` asynchronous readback; 100k-row reference benchmark.
- [x] Native smoke `GpuVisibilityCullsBinsAndDrawsIndirect` (vertex pulling through `GpuDrivenDraw.slang`) compiled; architecture verifier rules for `Renderer/Visibility`.
- [x] Execute the native smoke on the Windows desktop. *(2026-09-24: the full native suite passes `core`, `sync`, `gpu` and `all` on the Windows RTX 4070 Laptop.)*
- [ ] Execute it on a Linux desktop.
- [ ] Items 50/51 (HZB, occlusion with history invalidation) after the depth-convention gate.
- [ ] Item 56: construct `GpuScene` + `GpuVisibility` in the engine runtime and retire the CPU visible list.

### Items 50–51 HZB and occlusion checkpoint — 2026-09-23

Full contract: [GPU visibility](GpuVisibility.md#hzb-item-50).

- [x] Depth-convention gate: canonical reverse-Z (`DepthConvention.h`), reverse-Z orthographic/infinite perspective projections, `GpuViewFlags::ForwardDepth` for the opt-in forward mapping.
- [x] `HzbBuilder` + `HzbReduce.slang`: per-mip graph compute passes over a transient `R32Float` pyramid (farthest depth, odd sizes round up); `HzbReference` CPU definition with GPU readback adoption.
- [x] Two-phase occlusion in `GpuVisibility` (`VisibilityPhase::Early/Late`) with a persistent generation-tagged visibility history, a 1×1 stand-in HZB for the other phases, one persistent import per graph, and new statistics.
- [x] `VisibilityMath::OccludedByHzb` and `RunVisibilityReference` phases; the shader mirrors them.
- [x] Tests: depth convention, HZB reduction brute force, occlusion conservativeness against full-resolution depth, the CPU two-phase sequence, the 90k-object benchmark, mock early/late recording, reflected HZB/visibility layouts, the native two-phase smoke.
- [x] Execute the native smokes on the Windows desktop. *(2026-09-24: the full native suite passes `core`, `sync`, `gpu` and `all` on the Windows RTX 4070 Laptop.)*
- [ ] Execute them on a Linux desktop.
- [ ] GPU timing benchmark for occlusion on real content (the CPU benchmark shows the draw savings).
- [ ] Item 56: the engine runtime draws the world through `GpuScene` + `GpuVisibility`.

### Phase 13 exit criteria

- [ ] 100k mixed objects render without CPU visibility feedback.
- [ ] occlusion-heavy benchmark shows HZB benefit. *(CPU two-phase benchmark: 35.6% of 90k draws removed behind one wall; a GPU timing benchmark on real content is still needed.)*
- [x] camera cuts/teleports do not incorrectly occlude objects. *(Two-phase design; asserted on the CPU and by the native occlusion smoke, which passes all profiles on the Windows desktop.)*
- [x] GPU visibility stats are readable asynchronously for debugging. *(`VisibilityStats` through the readback arena, per phase.)*

---

## Phase 14 — Material system and physically based rendering

### Material architecture

```text
MaterialTemplate
  |-- Slang modules
  |-- legal features
  |-- parameter schema
  |-- pass participation
  `-- fixed render-state policy
        |
        v
MaterialInstance
  |-- scalar/vector parameters
  |-- texture handles
  |-- sampler handles
  `-- selected feature values
        |
        v
GpuMaterial
  |-- variant key
  |-- parameter buffer offset
  `-- bindless IDs
```

### Requirements

- [x] immutable shared templates; *(`MaterialTemplate`, shared as `std::shared_ptr<const MaterialTemplate>`)*
- [x] cheap mutable instances; *(`MaterialInstance`: one record copy, typed setters, change version)*
- [x] reflected typed parameters; *(`ShaderCompiler::BuildMaterialTemplateDesc` from structured-element reflection)*
- [x] GPU material parameter buffer; *(`GpuMaterialTable`, dirty-row uploads, timeline-retired rows)*
- [x] bindless textures/samplers; *(material records hold `BindlessResourceTable` indices; index 0 is the fallback)*
- [ ] alpha opaque/mask/blend/additive; *(opaque and mask in `StandardPbr`; blend/additive need pass participation)*
- [ ] double-sided/cull policy; *(the shader flips back-face normals for double-sided materials; pipeline cull selection comes with pass participation)*
- [ ] custom game material templates;
- [ ] hot reload;
- [x] deterministic fallback material; *(material row 0 = template defaults for unassigned/released/out-of-range indices)*
- [ ] no renderer-wide `switch` for every material feature;
- [x] no mesh ownership in material. *(`MaterialInstance` holds parameters only; GPU Scene rows pair a mesh with a material index)*

### PBR baseline

Implement glTF-compatible metallic-roughness PBR:

- base color;
- metallic/roughness;
- normal;
- occlusion;
- emissive;
- alpha mask/blend;
- double-sided;
- correct sRGB/linear semantics;
- Cook-Torrance;
- GGX;
- Smith geometry;
- Fresnel-Schlick;
- diffuse irradiance IBL;
- prefiltered specular IBL;
- BRDF integration LUT;
- HDR exposure/tone-map integration.

Optional material extensions come after the baseline is visually validated.

### Phase 14 exit criteria

- [x] PBR gallery matches reference expectations. *(Item 62: the CPU golden gallery meets furnace/monotonicity/occlusion expectations; the native gallery is compared per pixel — desktop run pending.)*
- [x] materials are independent from mesh assets.
- [x] shader reflection drives layout validation. *(`BuildMaterialTemplateDesc`; the built-in standard layout is checked against reflection)*
- [x] material changes update only affected GPU ranges. *(Version-driven dirty rows; asserted on the mock and native smokes.)*

---

## Phase 15 — Clustered Forward+ as the standard lighting path

Clustered Forward+ should be designed as a core renderer subsystem, not bolted onto a forward renderer later.

### Data flow

```text
GpuLightBuffer                 Depth/View
      |                           |
      +------------+--------------+
                   v
             Cluster grid
                   |
                   v
        GPU light assignment
                   |
                   v
       compact light-index lists
                   |
          +--------+--------+
          |                 |
          v                 v
      Opaque PBR       Transparent PBR
       Forward+           Forward+
```

### Cluster grid

- [x] configurable X/Y screen tiles;
- [x] configurable Z slices;
- [x] logarithmic/depth-aware Z partitioning;
- [x] view-relative bounds;
- [x] resolution changes regenerate grid parameters cleanly.

### Light assignment

- [x] point lights;
- [x] spot lights;
- [x] directional lights handled outside local cluster lists;
- [x] GPU cluster/light intersection;
- [x] compact index storage;
- [x] bounded overflow behavior;
- [x] overflow counters;
- [x] debug heatmap;
- [x] no per-object CPU light list.

### Transparency

Reuse clustered light lists for transparent Forward+ where valid rather than creating a second CPU lighting model.

### Performance goals

Characterize:

- 1k lights;
- 10k lights;
- higher stress counts for scaling analysis;
- mostly off-screen lights;
- dense local overlap;
- empty-light fast path.

Set practical configurable budgets from measurement rather than arbitrary hardcoded limits.

### Phase 15 exit criteria

- [x] thousands of dynamic lights scale predictably. *(Item 69 on the RTX 4070 Laptop, core validation: clustering takes 0.17 ms at 1k lights, 1.19 ms at 10k and 6.46 ms at 32k; overflowing clusters truncate and are reported, never written out of bounds. See [the items 66, 67, 69 record](validation/Items66-67-69-2026-09-23.md).)*
- [x] opaque and transparent rendering consume the clustered data path. *(items 66–67)*
- [x] overflow behavior is visible and safe. *(items 65/68: bounded lists, `ClusterStats`, magenta heatmap)*
- [x] zero/few-light scenes remain cheap. *(The empty scenario clusters in 0.053 ms on the RTX 4070 Laptop.)*

---

## Phase 16 — Shadows

### Directional

- [x] cascaded shadow maps or another stable first implementation; *(item 70: `ComputeCascades`, practical splits, rotation-invariant spheres; [Shadows](Shadows.md#directional-shadows-item-70))*
- [x] stable cascade snapping; *(centers rounded to whole light-space texels)*
- [x] GPU-driven caster culling; *(`GpuVisibility` per shadow view with `GpuViewFlags::ShadowCasters`)*
- [x] alpha-mask shadow variant; *(`SwimShadowMasked`, `SHADOW_ALPHA_TEST=1`; blended materials cast nothing)*
- [x] configurable cascade count/resolution. *(`CascadeSettings` 1–4 cascades, `CascadeResolution`)*

### Spot

- [x] shadow atlas allocator; *(item 71: `ShadowAtlasAllocator`, power-of-two tiles in one D32 atlas)*
- [x] stable allocation where possible; *(unchanged requests keep last frame's tiles, downgraded tiles grow back)*
- [x] budget/eviction policy; *(per-kind budgets by priority, downgrade → displacement → eviction to `ShadowKind::None`)*
- [x] GPU caster culling.

### Point

- [x] cube/array strategy only for selected shadow-casting lights; *(item 72: six cube faces as atlas tiles, only for `CastsShadows` lights with a slot)*
- [x] explicit cost controls. *(`MaxPointShadows`, per-light resolution, atlas downgrade/eviction, `ShadowPlanStats`)*

### Filtering/bias

- PCF baseline; *(done: (2R + 1)² exact comparisons)*
- slope/depth/normal-offset policy; *(done: receiver-side normal offset + slope term scaled by the kernel, constant depth bias)*
- later EVSM/VSM/PCSS experiments as modules.

### Integration

`GpuLight` stores a shadow handle/index. Clustered lighting does not need to know shadow implementation internals. *(Done: `GpuLightRecord::ShadowIndex` names a `GpuShadowRecord` slot; Forward+ calls `ShadowFactor` without knowing the kind.)*

---

## Phase 17 — Environment, HDR, and post-processing

### Environment

Rebuild cubemap/environment rendering as generic render passes/assets rather than backend-owned cube-map classes.

- [x] sky environment; *(`ProceduralSky` + `EnvironmentBuilder::RecordSky`, item 61)*
- [x] IBL irradiance; *(order-2 SH, `EnvironmentIrradiance.slang`)*
- [x] prefiltered specular environment; *(GGX filtered importance sampling, `EnvironmentPrefilter.slang`)*
- [x] environment rotation/intensity; *(`EnvironmentLighting`, applied in the lookup)*
- [ ] HDR environment asset support. *(`RecordFromSource` accepts any uploaded RGBA16Float cube; asset import/equirect conversion is not implemented.)*

### Post stack

Recommended order:

1. HDR scene color; *(done: Forward+ renders RGBA16Float; `PostProcessor` consumes it, items 73–74 — [Post-processing](PostProcess.md))*
2. exposure; *(done: histogram auto exposure with percentiles, adaptation and compensation, or manual EV100)*
3. tone mapping; *(done: Clamp, Reinhard, ACES fit, PBR Neutral; sRGB, HDR10 PQ and scRGB outputs)*
4. bloom; *(done: Karis-averaged 13-tap downsample and tent upsample chains)*
5. color grading; *(done: white balance, contrast, ASC CDL, saturation; a 3D LUT is later work)*
6. TAA once motion vectors/history are solid; *(done: Forward+ motion vectors and jitter, `TemporalAntiAliasing` with variance clipping, item 75 — [Temporal anti-aliasing](TemporalAntiAliasing.md))*
7. optional GTAO/SSAO; *(done: `ScreenSpaceEffects` GTAO on the Forward+ indirect radiance, item 76 — [Screen-space effects](ScreenSpace.md))*
8. optional SSR; *(done: a screen-space mirror march over the Forward+ normal + roughness, replacing the specular IBL through the new reflectance and specular targets, item 76 — [Screen-space effects](ScreenSpace.md#reflections-screenspacereflectionslang--screenspacereflectiontexel))*
9. fog; *(done: analytic exponential height fog with a sun lobe in the same composite, item 76)*
10. later volumetrics/DoF/motion blur as needed.

Every effect is a render-graph module and can be disabled cleanly.

---

## Phase 18 — Animation and skinning

### Asset data

Compiled model importer already provides:

- skeleton;
- joints;
- inverse bind matrices;
- clips/tracks;
- interpolation;
- morph targets.

*(2026-09-24, item 78: `Assets` skeleton/clip types, `Systems/Animation`, `Renderer/Skinning` + `Shaders/Slang/Skinning`; [Animation and skinning](Animation.md).)*

### Runtime

- [x] `SkeletonAsset`; *(asset type 7; the runtime `Skeleton` validates and shares it)*
- [x] `SkeletonInstance`; *(model-space joints, skinning palette + previous palette)*
- [x] `AnimationClip`; *(asset type 8; name-bound tracks, step/linear/cubic spline)*
- [x] `Animator`;
- [x] layers; *(override and additive, weights at run time)*
- [x] state machine; *(float/bool/trigger parameters, conditions, any-state, exit times)*
- [x] crossfade; *(linear; inertial blending remains)*
- [x] additive animation;
- [x] bone masks;
- [x] root motion; *(layer 0, per-axis translation and optional rotation, accumulated across loops)*
- [x] events; *(forward, reverse and across loops)*
- [x] playback speed/direction/looping;
- [x] sockets/attachments;
- [x] morph weights. *(clip tracks, defaults and manual overrides)*

### Jobs/GPU

- [x] jobify clip sampling/blending; *(`UpdateAnimations` over `JobSystem::ParallelFor`, bit-identical to serial)*
- [x] GPU skinning baseline; *(compute LBS + sparse morph targets into GeometryHeap output meshes)*
- [x] choose vertex vs compute skinning by measured workload; *(compute: skinned once per frame for every pass and view instead of per shadow/depth/Forward+ draw; 64 × 1,032 vertices time in the smoke. Visibility-driven skinning lists remain an optimization)*
- [x] preserve previous state for motion vectors; *(previous positions next to the current ones; Forward+ reads them through `GpuInstanceRecord::PreviousVertexOffset`)*
- [ ] explicit bridge to physics ragdolls later.

---

## Phase 19 — GPU particles

*(2026-09-24, item 77: `Renderer/Particles` + `Shaders/Slang/Particles`; [GPU particles](Particles.md).)*

- [ ] emitter assets/components; *(`ParticleEmitterDesc` + `ParticleSystem::CreateEmitter` are the runtime API; a cooked emitter asset and an ECS component arrive with engine wiring, item 56)*
- [x] GPU particle pool; *(persistent pool with per-emitter ranges, free lists, draw lists and counters; ranges retire after their last GPU use)*
- [x] GPU spawn command buffer; *(per-frame emitter records carry spawn counts and first ids; spawns pop the GPU free list)*
- [x] compute simulation; *(semi-implicit Euler with gravity and drag, `SwimParticleSimulate`)*
- [x] death/compaction; *(dead slots return to the free list; live slots compact onto the draw list)*
- [ ] billboard/mesh/trail rendering; *(camera-facing rotated billboards done; mesh particles and trails remain)*
- [x] local/world space;
- [x] curves; *(four-key size and RGBA color over life)*
- [x] flipbooks; *(over life or by frame rate, bindless sprites)*
- [x] optional collisions; *(a ground plane with restitution and friction; depth-buffer collisions remain)*
- [x] indirect generation; *(one `DrawIndexedIndirectCommand` per emitter from the finalize pass)*
- [x] transparency/sorting policy. *(additive unsorted; alpha-blended emitters sorted back to front per emitter, up to 2,048 particles; emitters ordered additive first, then blended back to front)*

Particles are a GPU Scene/render-graph producer, not thousands of normal EnTT mesh entities.

---

## Phase 20 — Runtime UI and text

### Text and retained UI foundation checkpoint — 2026-09-24

See [Text and retained UI](TextAndUi.md) for API contracts, build/test commands and remaining work. Item 79 is **partially implemented**, not checked off as a complete runtime renderer/widget toolkit.

### Separate UI from world Transform hacks

- [x] Dedicated retained `UiDocument`, document-owned nodes, `UiNodeId`, `UiStyle`, canvas layout and paint records, independent of Scene/Transform/RHI/SDL.
- [x] Safe hierarchy ownership, cycle/depth checks, subtree removal and stale/cross-document ID rejection.
- [x] Clipped scrolling, hit-testing in reverse paint order, pointer capture/cancellation, focus traversal and activation through queued events.
- [ ] Widget layer: `UiImage`, `UiText`, `UiButton`, `UiScrollView`, `UiInputField`. *(Text, button activation and scroll mechanics exist on generic nodes; dedicated widgets, editing and image resources remain.)*
- [ ] Platform/Input adapter and sandbox migration. *(Consumers currently call the document directly.)*

### Layout

- [x] measure -> layout -> paint;
- [x] parent/child hierarchy;
- [ ] anchors;
- [x] margin/padding;
- [x] min/max/preferred size;
- [x] row/column stacks and overlay layout; *(flex grow/shrink/alignment remain)*
- [x] absolute placement;
- [ ] aspect ratio;
- [x] percentage sizing; *(resolved against final parent content; percentage contributions on an intrinsic parent axis are zero during measurement)*
- [x] DPI-aware logical units;
- [x] scrolling/clipping;
- [x] document-level dirty layout invalidation and reuse of unchanged shaped text;
- [ ] subtree-only layout invalidation and cached paint-list updates. *(A dirty document currently recomputes layout; Paint rebuilds output.)*

### Text

- [x] FreeType scalable Unicode OpenType font loading, metrics and outline decomposition;
- [x] HarfBuzz horizontal run shaping;
- [x] UTF-8 byte clusters, combining marks, ligatures, kerning and non-BMP code points;
- [ ] font fallback; *(missing glyphs are counted, never silently substituted by an OS font)*
- [x] homogeneous RTL/script runs; *(Arabic contextual shaping covered)*
- [ ] paragraph bidi/script segmentation; *(explicit run-level contract; HarfBuzz guessing is not a bidi algorithm)*
- [ ] Unicode line breaking/wrapping; *(LF/CRLF hard lines only)*
- [ ] selection/caret;
- [ ] IME visualization;
- [x] MSDF atlas pages, bounded packing, stable UVs and page revisions;
- [ ] async atlas generation, GPU uploads/residency and timeline-safe atlas retirement.

### Rendering

- [x] Backend-neutral ordered solid/glyph quad output with clip rectangles, UVs and MSDF distance range;
- [ ] batched instanced quad RenderGraph pass;
- [ ] bindless images/glyph atlases;
- [ ] rounded/SDF primitives;
- [ ] borders/nine-slice;
- [ ] GPU clipping/scissor indexing; *(CPU paint/hit-test clip intersections exist)*
- [x] premultiplied linear alpha paint policy;
- [ ] HDR-aware composition and native image-regression smoke.

World-space text can reuse the new shaping/atlas services while producing world render instances. The current sandbox still uses legacy text/UI; this checkpoint does not rewire it.

---

## Phase 21 — Audio

Use miniaudio behind a Swim API.

- [ ] audio device;
- [ ] sound asset vs playing voice;
- [ ] one-shot/looping;
- [ ] streaming;
- [ ] buses;
- [ ] listener/source 3D audio;
- [ ] attenuation;
- [ ] Doppler;
- [ ] pitch/pan/spread;
- [ ] priorities/voice stealing;
- [ ] device hot-change;
- [ ] async decode/stream jobs.

Audio assets participate in the same AssetId/dependency model.

---

## Phase 22 — Legacy OpenGL and editor/tooling compatibility

### OpenGL stays functional but isolated

Move the existing OpenGL renderer under a clearly legacy module, for example:

```text
Source/Swim/Legacy/OpenGL/
```

Rules:

- [ ] it uses Platform window/input abstractions;
- [ ] it no longer forces generic `Vertex`, `Texture`, `Camera`, or `Transform` types to contain GL behavior;
- [ ] new renderer features do not require OpenGL parity;
- [ ] basic legacy mesh/text/cubemap/debug functionality remains usable;
- [ ] Slang-generated shader output is used where practical;
- [ ] OpenGL-specific types stay in the legacy implementation.

### Editor code is preserved where useful, but not foundational

Do not delete editor systems just because they are not part of the runtime core.

Reclassify them:

- gizmos -> generic debug/tooling module;
- editor camera -> debug/tool camera;
- serialized scene manager -> authoring/serialization service;
- editor commands -> tooling command bridge;
- `WM_COPYDATA` -> Windows-specific editor transport adapter;
- embedded child window -> Platform external-window support.

The engine runs fully with these modules absent/unwired.

### Generic tooling transport

If editor communication remains important, define a transport-neutral command/event interface and let Win32 IPC be one adapter.

Possible future adapters can include local sockets/pipes without changing scene/engine code.

---

# Part IV — Cross-cutting architecture rules

## 27. Public API and lifetime rules

- [ ] PascalCase public naming.
- [ ] explicit owner for every long-lived subsystem.
- [ ] no mandatory mutable global singleton.
- [ ] typed generational handles for long-lived registries.
- [ ] move-only RHI owners where appropriate.
- [ ] no raw pointer as persistent public identity.
- [ ] callbacks/subscriptions return removable tokens.
- [ ] async operation documents completion executor and cancellation.
- [ ] GPU resource documents retirement behavior.
- [ ] backend-native handles are explicit escape hatches.
- [ ] config structs have stable defaults.
- [ ] no constructor hides disk IO or GPU submission.
- [ ] no component owns renderer/backend implementation objects.

---

## 28. Threading model

Start simple and explicit.

### 28.1 Main/platform thread

Owns:

- SDL event pump;
- main window creation/destruction;
- platform operations that require main thread;
- application frame orchestration.

### 28.2 Jobs

Use general workers for:

- render extraction preprocessing;
- animation;
- asset decompression/processing;
- scene CPU tasks;
- compiler/tool work;
- suitable physics integration tasks.

### 28.3 Rendering

Do not create a dedicated render thread merely because engines often have one.

First build a clean frame ownership model. Add dedicated render-thread submission or parallel command recording only when profiling and latency goals justify it.

### 28.4 Data exchange

Prefer:

- stable handles;
- immutable snapshots;
- dirty command streams;
- per-frame arenas;
- double/triple-buffered GPU update regions;
- task dependencies.

Avoid a giant global renderer mutex.

---

## 29. Performance rules

- [ ] no per-object API draw call in the normal world path;
- [ ] no descriptor set per object;
- [ ] no CPU visibility readback required for drawing;
- [ ] no full-scene upload for a few dirty transforms;
- [ ] no full-material buffer upload for one material change;
- [ ] no synchronous asset disk IO in ordinary frame update;
- [ ] no runtime mesh optimization for compiled assets;
- [ ] no shipping frame-time shader compilation;
- [ ] no steady-state device/queue idle;
- [ ] no per-frame rebuild of static render objects;
- [ ] no global shared-pointer graph walk in renderer hot path;
- [ ] use packed GPU records and SoA where high-volume access benefits;
- [ ] batch transfers/barriers;
- [ ] profile before adding exotic GPU paths.

---

## 30. Debug/profiling systems

Build diagnostics into the architecture instead of relying on editor state.

- [ ] Tracy CPU zones;
- [ ] GPU zones/timestamps;
- [ ] RenderDoc markers/capture trigger;
- [ ] render graph viewer/dump;
- [ ] GPU Scene counts;
- [ ] visibility counters/heatmaps;
- [ ] HZB viewer;
- [ ] cluster light heatmap;
- [ ] cluster overflow stats;
- [ ] shadow atlas/cascade viewer;
- [ ] texture/buffer viewer;
- [ ] VRAM budget/allocation view;
- [ ] asset streaming stats;
- [ ] job queue stats;
- [ ] physics backend name/stats.

---

## 31. Required benchmark/demo ladder

Build these in the same order as the engine so every phase has a proof target.

1. [ ] Headless Core/Jobs test
2. [ ] Cross-platform window/input
3. [ ] Slang reflection sample
4. [ ] RHI clear
5. [ ] RHI triangle
6. [ ] RHI texture
7. [ ] compiled static mesh
8. [ ] 100k GPU Scene instances
9. [ ] GPU frustum culling
10. [ ] HZB occlusion hall
11. [ ] PBR gallery
12. [ ] Clustered Light Storm
13. [ ] shadow courtyard
14. [ ] HDR/post lab
15. [ ] animated character
16. [ ] skinned crowd
17. [ ] GPU particle storm
18. [ ] runtime UI gallery
19. [ ] PhysX/Jolt physics sandbox
20. [ ] spatial audio scene
21. [ ] streaming city
22. [ ] multi-window/editor-host sample

---

## 32. Tests and CI

### 32.0 How tests are organized

**One program.** The whole runnable test corpus is a single executable, `SwimTests`. Coverage below is a description of what that program must contain, not a list of binaries to create.

```text
Source/Tests/
  Framework/        registry, checks, CLI runner, the single main()
  Suites/           test cases, grouped by dependency
  Fixtures/         shared multi-suite helpers (header-only)
  HeaderBoundary/   per-module public-header compile gates
```

**Adding a test is not a build-system change.** Cases self-register through static initializers, so a new `.cpp` under `Source/Tests/Suites/<group>/` containing `SWIM_TEST("Suite", "Case") { ... }` is picked up by the next configure. There is no central list, no per-test `main()`, and no new target.

**The suite group is the dependency contract.** `Suites/Core|Memory|Jobs|IO|Input|Assets`, `Suites/Physics/Generic`, and `Suites/Scene/Headless` compile in every configuration. `Suites/AssetCompiler`, `Suites/Scene/Ecs`, and `Suites/Physics/PhysX` compile only where their dependency targets exist. A Linux foundation build therefore runs the portable suites and omits the rest, which keeps the cross-platform matrix in 32.10 honest without a second test program.

**Checks never compile away.** Swim defines `NDEBUG` in every configuration including Debug, so `assert()` is a no-op everywhere. Test code uses `SWIM_CHECK*` (record and continue) and `SWIM_REQUIRE*` (record and abandon the case). Never `assert()`.

**The runner is the selection mechanism.** Filtering happens at run time rather than by choosing a binary:

```text
SwimTests --list
SwimTests --filter=Physics
SwimTests --exclude="*Draco*" --stop-on-failure
SwimTests --shuffle --repeat=5
SwimTests --report=results.xml
```

An empty selection exits non-zero, so a mistyped filter in a build script cannot be mistaken for a passing run. `--report` emits JUnit XML for CI consumption.

**Header-boundary gates are not part of `SwimTests`.** `SwimPlatformPublicHeaders`, `SwimIoPublicHeaders`, `SwimAssetPublicHeaders`, `SwimAssetCompilerPublicHeaders`, `SwimPhysicsPublicHeaders`, and `SwimPhysicsBackendContractCompile` are small object libraries that each link exactly one module. That narrow link surface is the whole point: `SwimTests` links everything, so folding them in would destroy the guarantee. Declare new ones with `swim_add_header_boundary()`.

**Build scripts run everything.** The Windows clean/soft builds and both Linux builds build and run `SwimTests` in full. Do not reintroduce per-phase target lists in the build scripts; use a `--filter` if a narrower gate is ever genuinely needed.

### 32.1 Platform

- window lifecycle;
- external-window wrap where supported;
- pixel/logical size;
- DPI;
- focus;
- input hotplug;
- text/IME;
- filesystem roots;
- mapped file;
- dynamic library.

### 32.2 Jobs/IO

- dependencies;
- cancellation;
- nested task submission;
- shutdown;
- range reads;
- concurrent reads;
- failed IO.

### 32.3 Assets

- AssetId stability;
- malformed files;
- schema mismatch;
- dependency graph;
- incremental compile;
- package lookup;
- source import corpus;
- deterministic compiled output;
- cancellation;
- placeholder/failure path.

### 32.4 Scene

- create/destroy;
- scene-local transform dirty state;
- hierarchy;
- multiple scenes;
- mutation command buffer;
- CPU BVH queries.

### 32.5 Physics

Run the same behavioral suite against PhysX and Jolt. The suite is
`Source/Tests/Fixtures/PhysicsBackendContract.h`, which exposes:

| Contract | Covers |
| --- | --- |
| `RunPhysicsWorldLifecycleContract` | creation, kinematic targeting, handle validity, deferred destruction, slot reuse |
| `RunPhysicsSceneQueryContract` | raycast/sweep/overlap, layer filtering, generic hit identity, query-shape local poses |
| `RunPhysicsSimulationContract` | gravity, collision-start events, velocity and force application |
| `RunPhysicsTriggerContract` | trigger enter/exit for a body passing through a sensor |
| `RunPhysicsSharedShapeContract` | one `ShapeHandle` reused by several bodies with independent collision layers |
| `RunPhysicsInFlightWriteContract` | mutators rejected while a step is in flight; non-blocking fetch makes progress |
| `RunPhysicsContactEventContract` | persisted-event opt-in and non-zero contact impulses |

Each builds its own world from a shared fixture, so scenarios stay independent and
can run in any order. A backend adds `Suites/Physics/<Backend>/…Tests.cpp`
registering one case per entry point and changes nothing else.

These are behavioural specifications, not descriptions of one backend. Where the
two implementations disagreed, the contract was extended and the weaker side was
corrected rather than the assertion relaxed.

### 32.6 RHI

Run common RHI tests against each modern backend as backends arrive:

- buffer copy;
- texture copy;
- barriers/resource states;
- timeline retirement;
- descriptors;
- pipeline creation;
- swapchain resize;
- query/timestamps.

### 32.7 RenderGraph

- dependency ordering;
- cycles;
- read-before-write;
- barrier synthesis;
- pass culling;
- resource lifetime;
- imported/exported resources;
- staged upload/readback suballocation, growth, failure recovery and partial preserving copies (`RenderGraph.Transfers`).

GPU residency layers have their own groups: `RenderScene` (`Render.GpuScene`, `Render.GpuSceneStress`), `RenderResources` (`Render.GpuResourceRegistry`, `Render.Bindless`, `Render.SamplerCache`), `RenderGeometry` (`Render.GeometryRangeAllocator`, `Render.GeometryHeap`) and `RenderResidency` (`Render.MeshGeometryPayload`, `Render.TextureResidency`, `Render.AssetResidency`, compiled with the IO/Platform foundation). Cases that cook real `.sasset` objects live in the `AssetCompiler` group. Vulkan bindless layout/table capture cases are `RHI.Vulkan.Bindless` in the `RHIVulkan` group. EnTT extraction (`Scene.RenderExtraction`) is in `Scene/Ecs`, built wherever EnTT is configured. Material templates/instances and the PBR model have `RenderMaterials` (`Render.Materials`, `Render.StandardPbr`), the GPU material table has `RenderGpuMaterials` (`Render.GpuMaterials`), and reflection conversion is in `ShaderCompiler.MaterialLayout`. GPU visibility has `RenderVisibility` (`Render.Visibility`, `Render.GpuVisibility`, `Render.DepthConvention`, `Render.Hzb`, `Render.Occlusion`); indirect-draw capture cases are `RHI.Vulkan.IndirectDraw` in `RHIVulkan`.

### 32.8 GPU Scene/visibility

- stable handle reuse;
- dirty range correctness;
- large counts;
- frustum;
- camera cuts;
- HZB conservative cases;
- LOD;
- indirect count.

### 32.9 Rendering

- image regression PBR;
- sRGB/linear;
- normal maps;
- IBL;
- clustered assignment;
- overflow;
- shadow regressions;
- post effects.

### 32.10 Cross-platform matrix

First-class:

- Windows MSVC;
- Windows clang-cl where useful;
- Linux Clang;
- Linux GCC.

Later add Apple/Android configurations when those ports begin.

Build/dependency matrix requirements:

- [ ] CMake target-boundary consumer test: a small external-style application links only public Swim targets.
- [ ] Configuration matrix covers enabled/disabled Vulkan, OpenGL legacy, PhysX, and Jolt combinations where supported.
- [ ] Generated Slang and asset build dependencies rebuild deterministically from changed source inputs.
- [ ] No backend/importer library becomes an unintended transitive dependency of generic targets.
- [x] The runnable test corpus is one program; adding a test requires no CMake change.
- [x] Every supported build script builds and runs the complete suite rather than a hand-maintained subset.
- [x] Public-header/architecture gates keep a link surface narrower than the test program's.
- [x] A second physics backend reuses the shared contract fixture unchanged. *(Jolt registers the same seven contract entry points as PhysX.)*

---

# Part V — Repository organization target

## 33. Suggested source layout

The exact folder names can vary, but dependency direction should be visible in the tree. Within any one module or backend, §0.2's file organization rule applies: one concrete type per file, shared helpers in their own `Internal/` header, and files grouped into plain role-named subfolders rather than one large file per module. The current `RhiVulkan`, `Physics/Backends/Jolt`, and `Physics/Backends/PhysX` backends are the worked examples — each looks like this instead of one monolithic `.cpp`:

```text
Backends/Vulkan/
  Internal/     shared bootstrap state, native-handle helpers, format conversion
  Resources/    VulkanBuffer, VulkanTexture, VulkanTextureView, VulkanSampler
  Sync/         VulkanSemaphore, VulkanFence, VulkanTimeline
  Commands/     VulkanCommandPool, VulkanCommandList, VulkanCommandPoolState
  VulkanDevice.h / VulkanAdapter.h / VulkanSwapchain.h+.cpp / VulkanQueue.h+.cpp / VulkanGraphicsSystem.h
  VulkanRhiBackend.h/.cpp   thin: instance bootstrap + factory registration only

Physics/Backends/<Jolt|PhysX>/
  Internal/     shared, backend-private math/validation helpers (own namespace per backend)
  Filters/      query/collision filter callback types
  Callbacks/    engine-to-third-party event callback types
  <Backend>WorldBackend.h/.cpp   the backend's own class only
  <Backend>Backend.h/.cpp, <Backend>BackendFactory.h/.cpp   already small; left as-is
```

New backends and new large systems should be organized the same way from the start rather than growing one file per module and splitting it later.

```text
Source/Swim/
  Core/
  Platform/
  Input/
  Jobs/
  Io/
  Assets/
  Scene/
  Physics/
    PhysX/
    Jolt/
  Rhi/
  RhiVulkan/
  Render/
    Graph/
    Scene/
    Visibility/
    Geometry/
    Material/
    Lighting/
    Shadows/
    Environment/
    Particles/
    Post/
    Debug/
  Animation/
  Audio/
  Ui/
  Engine/
  Legacy/
    OpenGL/
  Tooling/
    EditorBridge/

Tools/
  SwimAssetCompiler/
    Cli/                 command-line front ends for this module
  SwimShaderCompiler/
  SwimPack/

Apps/
  Sandbox/

Examples/
  HelloWindow/
  RhiTriangle/
  PbrGallery/
  ClusteredLights/
  GpuSceneStress/
  PhysicsSandbox/
  UiGallery/
  StreamingScene/

Tests/
  Framework/             registry, checks, CLI runner, single main()
  Suites/<group>/        self-registering cases, grouped by dependency
  Fixtures/              shared multi-suite helpers
  HeaderBoundary/        per-module public-header compile gates
```

Build targets should mirror these module boundaries so dependency direction is visible and enforceable.

This folder tree is a *source-organization* boundary (§0.2) and stays accurate regardless of CMake target topology; whether a given folder also happens to be a separate CMake target is a build-graph decision, not a source-layout one. As of the 2026-09-05 engine module collapse (§4.3), most of `Source/Swim/...`'s current-state equivalent (`Core`, `Platform`, `Input`, `Jobs`, `Io`, `Assets`, `Physics`, `Rhi`, `RhiVulkan`) compiles directly into `SwimEngine` rather than into one CMake target per folder — dependency direction between them is still enforced by code review and namespace/include discipline, not by the linker. `Tests/`, `Tools/`, and `Examples/` are unaffected by that collapse and are still the exception to "one directory, one target" described below.

`Tests/` is the deliberate exception to "one directory, one target". Everything under `Suites/` compiles into the single `SwimTests` program, and the directory a suite sits in expresses which dependency group it belongs to rather than which target it builds. A tool that owns a `main()` lives under its module's `Cli/` directory for the same reason: the extra CMake target is a link-time necessity, not a second module.

---

## 34. Current-code integration map

| Current code | Target treatment |
| --- | --- |
| `SwimEngine.*` | Split window/platform work, runtime composition, config parsing, game loop, and optional editor bridge. Remove global service-locator role. |
| `Machine.h` | Keep only as an optional lifecycle convenience; core services do not all need to derive from it. |
| `SystemManager.*` *(retired; see §0.3)* | Core service ownership uses explicit typed composition; the unused dynamic registry is archived. |
| `PCH.*` | Remove OS/RHI/backend leakage. |
| `InputManager.*` *(retired; see §0.3)* | Replace with SDL3-backed generic input/event/action-map system. |
| `CommandSystem.*` | Keep useful command registry; detach it from editor transport and global engine. |
| `Scene.*` | Keep EnTT scene concept; remove concrete renderer pointers/global dependency discovery; own scene-local systems cleanly. |
| `SceneSystem.*` | Become SceneManager/runtime orchestration; replace static live-scene preregistration with explicit SceneCatalog factories and remove backend-specific wiring. |
| `EntityFactory.*` | Replace global active-scene queues with scene-owned command/mutation buffer. |
| `Behavior*` | Keep lifecycle scripting; replace global factory/service access with owned registry + explicit context. |
| `Transform.*` | Keep transform/hierarchy math; move static dirty state to scene TransformSystem; remove render-API logic and active-scene discovery. |
| `CameraSystem.*` | Convert to reusable camera/view data; remove global engine access and API-specific projection branch. |
| `Frustum.h` | Keep math; make instances per view rather than static global state. |
| `SceneBVH.*` | Retain as the CPU query/debug/reference structure; remove responsibility for the final visible draw list. |
| `SceneDebugDraw.*` | Keep concepts; render through generic Debug/RenderGraph path and asset handles. |
| `GizmoSystem.*` | Move to optional tooling/debug module. |
| `SerializedSceneManager.*` | Split into pure scene serialization, scene storage, and optional tooling/editor transport; persist stable entity IDs and `AssetId` references rather than runtime EnTT IDs/pool-derived paths. |
| `MeshPool.*` | Replace singleton with AssetSystem + runtime mesh registry/residency. |
| `Mesh.*` | Split source/CPU mesh asset data from GPU residency. |
| `MeshBufferData.*` | Replace with backend-neutral MeshAsset metadata plus RHI GeometryHeap residency metadata. |
| `Vertex.h` | Make pure CPU vertex/data declarations; move Vulkan descriptions and GL attribute setup into backends/pipeline schemas. |
| `TexturePool.*` | Replace singleton/eager recursive loading with AssetSystem + streaming/residency. |
| `Texture2D.*` | Split CPU texture asset from RHI texture resource; eliminate constructor IO/upload/static cleanup set. |
| `MaterialPool.*` | Remove runtime importer/global singleton; source import goes to compiler, runtime gets MaterialTemplate/Instance registries. |
| `MaterialData.h` | Replace mesh-owning material bundle with independent material template/instance data. |
| `Material` component | Store `MaterialHandle`/material-set handle rather than shared owning pointer. |
| `CompositeMaterial` | Replace with ModelAsset/submesh material slots and instance overrides. |
| `FontPool.*` | Move to AssetSystem/text service; load on demand. |
| `TextComponent.h` | Keep high-level text state if useful, replace handwritten shaping/UTF pipeline with text service. |
| `TextLayout.h` | Rebuild backend-neutral shaping/layout output; renderer consumes glyph instances through generic structures. |
| `CubeMap*` | Replace backend-specific asset ownership with Environment assets/passes. |
| `Renderer.h` | Split the mixed facade: presentation goes to Platform/RHI swapchain, environment/UI policy stays high-level, asset upload goes through residency/transfer services, and the modern low-level contract is the RHI. |
| `VulkanDeviceManager.*` | Rebuild as `RhiVulkan::Device/Adapter` using volk/vk-bootstrap. |
| `VulkanCommandManager.*` | Rebuild into RHI queues/command allocators/frame contexts. |
| `VulkanDescriptorManager.*` | Rebuild as RHI resource binding + bindless tables informed by Slang reflection. |
| `VulkanPipelineManager.*` | Rebuild as shader/pipeline cache keyed by reflected program + fixed state. |
| `VulkanSyncManager.*` | Replace fence-centric ownership with timelines/deferred retirement/render-graph state tracking. |
| `VulkanSwapChain.*` | Become Vulkan RHI swapchain using Platform window abstraction. |
| `VulkanBuffer.*` | Replace manual memory ownership with RHI buffer + VMA implementation. |
| `VulkanInstanceBuffer.*` | Absorb into GPU Scene upload/storage. |
| `VulkanGpuInstanceData.h` | Preserve useful record ideas, redefine as backend-neutral renderer GPU schemas generated/validated against Slang layouts. |
| `VulkanIndexDraw.*` | Decompose into GeometryHeap, GPU Scene, Visibility, indirect generation, render passes, and debug statistics. Preserve stable-slot/indirect concepts. |
| `VulkanCubeMap.*` | Replace with Environment render module and generic RHI resources. |
| `VulkanRenderer.*` | Replace monolith with RenderSystem + RenderGraph + RHI Vulkan implementation modules. |
| `OpenGL/*` | Keep functional under `Legacy/OpenGL`; remove influence on modern generic data structures. |
| `PhysicsSystem.*` | Become generic physics facade/factory; move PhysX creation into backend. |
| `PhysicsWorld.*` | Split generic world API from PhysX world implementation. |
| `RigidBody.h` | Remove Px pointers; store generic body handle/config only. |
| `ParallelUtils.h` | Replace renderer-global pool with general Jobs service; port useful parallel algorithms. |
| `Source/Shaders/Vulkan/*` | Slang-only first-party Vulkan source; build emits SPIR-V + reflection. |
| `Source/Shaders/OpenGL/*` | Slang-only source for the isolated legacy backend; build emits GLSL compatibility artifacts. |
| `Source/Game/*` | Update incrementally to use EngineServices, generic Input, AssetHandle, MaterialHandle, and generic Physics APIs. |

---

# Part VI — Exact implementation order / PR ladder

## 35. Critical path

This is the recommended order for actual implementation. Do not skip ahead to a major renderer feature if an earlier contract it depends on is still temporary. Every PR should preserve the CMake target/dependency rules in Section 4.2 as it introduces new platform code, libraries, generated artifacts, or backend implementations.

### 35.1 Foundation

1. [ ] Create clean `Core` public include boundary and remove backend/platform dependencies from generic PCH. *(The generic PCH leakage is already removed, but the final `Core` public include/module boundary is not complete.)*
2. [x] Add SDL3-backed `PlatformSystem` and `Window` abstraction.
3. [x] Move Win32 window creation/message handling out of `SwimEngine`.
4. [x] Implement Windows external/native-window wrapping needed by editor hosting.
5. [ ] Add Linux window path and `HelloWindow` test. *(The cross-platform implementation/test target exists; the same real SDL-backed runtime smoke still needs to be executed on both Windows and Linux.)*
6. [x] Replace `InputManager` with SDL3 normalized InputSystem + action API. *(Engine/Scene/Behavior/gameplay now consume `Swim::Input::InputSystem` directly; the wrapper and old accessors are retired. See section 0.3.)*
7. [x] Add filesystem roots/path/mapped-file/dynamic-library platform APIs.
8. [x] Replace global `SwimEngine` dependency discovery with explicit Engine composition/services.
9. [x] Add runtime `GraphicsBackend` and `PhysicsBackend` config/launcher parsing.
10. [x] Introduce enkiTS-backed Jobs service and retire renderer-global worker ownership.
11. [x] Add async/range IO service.

### 35.2 Data and scene foundations

12. [x] Introduce `AssetId`, typed `AssetHandle<T>`, asset registry, load state, dependency graph.
13. [x] Split Mesh/Texture/Material CPU identity from GPU/backend resources.
14. [x] Remove mesh ownership from material data.
15. [x] Build fastgltf-based source importer + intermediate model representation.
16. [x] Add meshoptimizer offline processing.
17. [x] Add KTX2 compiler/runtime metadata path.
18. [x] Define `.sasset` v1 and compile/load one static model.
19. [x] Replace global asset pools with engine-owned asset services. *(The engine-owned `AssetSystem` is authoritative; legacy mesh/texture/material pools remain only as engine-owned renderer residency compatibility surfaces.)*
20. [x] Replace static transform dirty state with scene-owned TransformSystem.
21. [x] Replace static global Frustum with per-view state.
22. [x] Replace global EntityFactory queue with scene command buffer.
23. [x] Replace static live-scene preregistration with explicit SceneCatalog factories and loaded `SceneHandle` identity. *(Implemented with runtime `SceneId` identity.)*
24. [x] Split scene serialization/storage/tooling transport; add durable entity IDs and `AssetId` scene references.
25. [x] Remove renderer backend pointers from Scene/Behavior APIs.
26. [x] Establish canonical coordinate/clip-space convention.
27. [x] Build generic physics handles/contracts.
28. [x] Move current PhysX implementation behind generic backend.
29. [x] Add Jolt backend baseline and shared parity tests.

### 35.3 Shader/RHI foundation

30. [x] Integrate Slang compiler and reflection metadata.
31. [x] Port all first-party shader source to Slang; retire first-party HLSL/handwritten GLSL and DXC compilation.
32. [x] Define RHI formats/resource states/descriptors/capability table.
33. [x] Define RHI Device/Queue/Swapchain/Buffer/Texture/Sampler/Pipeline contracts.
34. [x] Add runtime graphics factory.
35. [x] Build Vulkan RHI instance/adapter/device using volk + vk-bootstrap.
36. [x] Create Vulkan surface from Platform window through SDL3 WSI.
37. [x] Add VMA buffer/image allocation.
38. [x] Add timeline/frame-context/deferred-destruction model.
39. [x] Validation-clean RHI clear/triangle/texture on Windows and Linux. *(All nineteen native cases pass core, synchronization, GPU-assisted and combined profiles: Windows RTX 4070 hardware and Linux Mesa llvmpipe software Vulkan on Xvfb/Openbox. Windows strict HDR passes separately. The exact user-approved GPU-AV descriptor-limit startup advisory is retained; every other warning/error/drop fails. See [September 12 evidence and reproduction](validation/Item39-2026-09-12.md). Linux physical-GPU coverage remains additional coverage, not claimed by this checkpoint.)*

### 35.4 Modern renderer foundation

40. [x] Implement RenderGraph DAG/resource-state/barrier system. *(Validated graph compiler, single-graphics-queue executor, pooling, import/export lifetime, GPU diagnostics and native render/compute consumers; see the Phase 10 checkpoint and [evidence](validation/Item40-2026-09-12.md). Dedicated ownership transfers/async scheduling remain explicit Phase 10 follow-ups.)*
41. [x] Implement upload/readback arenas and transfer helpers. *(Persistent mapped arenas and frame-ring integration from Phase 9, plus graph-declared staged uploads/readbacks with executor-owned arenas, completion-bound readback batches and whole/partial buffer/texture transfer passes. Windows native smokes pass all four profiles; Linux desktop execution and dedicated transfer-queue ownership remain open; see the item 41 checkpoint in Phase 10.)*
42. [x] Implement generational GPU resource registries. *(`GpuHandle<Tag>` + timeline-retiring `GpuResourceRegistry`; see the Phase 11 item 42–43 checkpoint.)*
43. [x] Implement paged GeometryHeap. *(Paged residency with graph-recorded uploads; Windows native smoke passes all four profiles; submeshes added with item 44; see [GPU residency](GpuResidency.md).)*
44. [x] Connect compiled MeshAsset/TextureAsset to asynchronous GPU residency. *(`AssetResidencyService` + `TextureResidency` + GeometryHeap submeshes; uncompressed textures only, ModelAsset requests and engine wiring pending; see the Phase 11 item 44 checkpoint.)*
45. [x] Implement bindless texture/sampler table with timeline-safe ID reuse. *(`BindlessResourceTable` + `GpuSamplerCache` on new RHI bindless spaces; native smoke pending desktop execution; see the Phase 11 item 45 checkpoint.)*
46. [x] Implement persistent GPU Scene records. *(`GpuScene` + `GpuRecordBuffer`, shared Slang records with a reflected layout check; see the Phase 12 checkpoint.)*
47. [x] Implement EnTT render extraction -> RenderObject updates. *(`RenderExtractor` over `MeshRenderer` and the TransformSystem dirty list; not yet constructed by the runtime.)*
48. [x] Reach 100k object GPU Scene stress with dirty-only updates. *(CPU stress for GpuScene and extraction plus the native 100k-row probe smoke, which passes all four profiles on the RTX 4070.)*

### 35.5 GPU-driven renderer

49. [x] GPU frustum culling. *(2026-09-23: `GpuVisibility` sphere/plane culling over `GpuScene`; [GPU visibility](GpuVisibility.md). Native smoke passes all profiles on the Windows desktop.)*
50. [x] depth/HZB build using final reverse-Z/depth convention. *(2026-09-23: canonical reverse-Z `D32Float`; `HzbBuilder` + `HzbReduce.slang`; [GPU visibility](GpuVisibility.md#hzb-item-50). Native smoke passes all profiles on the Windows desktop.)*
51. [x] occlusion culling with history invalidation. *(two-phase early/late culling against the current frame's HZB; generation-tagged visibility history; `CameraCut` resets it)*
52. [x] LOD selection/hysteresis. *(projected mesh-LOD error, per-row history, reset on generation change or `ResetLodHistory`)*
53. [x] visible compaction. *(atomic per-bin slots)*
54. [x] material/pass binning. *(material bins × index-page slots with bounded capacity; per-pass bins arrive with shadow/depth views)*
55. [x] indirect command/count generation. *(`DrawIndexedIndirectCommand` + `GpuDrawRecord` per submesh, per-bin counts for `DrawIndexedIndirectCount`)*
56. [ ] remove CPU-visible-list dependency from normal world rendering.
57. [x] add visibility diagnostics/benchmarks. *(`VisibilityStats` async readback; `Render.Visibility.HundredThousandObjectBenchmarkKeepsStatisticsConsistent`)*

### 35.6 Shading and lighting

58. [x] MaterialTemplate/MaterialInstance + reflected parameters. *(2026-09-23: CPU data layer and tool-side reflection conversion; [Materials](Materials.md). Features/variants, pass participation and render-state policy arrive with items 59–60.)*
59. [x] GPU material buffer/bindless material resources. *(2026-09-23: `GpuMaterialTable`; [Materials](Materials.md#gpu-material-table-item-59). Native smoke passes all profiles on the Windows desktop.)*
60. [x] metallic-roughness PBR. *(`StandardPbr` CPU definition + `StandardPbr.slang`; IBL is item 61, tone mapping item 73)*
61. [x] environment/IBL. *(2026-09-23: `Renderer/Environment` + `Shaders/Slang/Environment`; [Environment](Environment.md). HDR environment assets remain; tone mapping arrived with item 73.)*
62. [x] PBR image regression gallery. *(2026-09-23: CPU golden renderer `PbrGalleryFixture.h` + native `PbrGalleryMatchesTheCpuReference`; [Environment](Environment.md#pbr-image-regression-gallery-item-62).)*
63. [x] GpuLightBuffer. *(2026-09-23: `Renderer/Lights` + `GpuLightRecords.slang`; [GPU lights](Lights.md). Native smoke passes all profiles on the Windows desktop.)*
64. [x] clustered grid. *(2026-09-23: `Renderer/ClusteredLighting/ClusterGrid`; [Clustered lighting](ClusteredLighting.md). Native smoke passes all profiles on the Windows desktop.)*
65. [x] GPU light assignment/compaction. *(2026-09-23: `ClusteredLightAssigner` + `Shaders/Slang/ClusteredLighting`.)*
66. [x] opaque Clustered Forward+. *(2026-09-23: `Renderer/ForwardPlus` + `ClusteredForward.slang`; [Clustered Forward+](ForwardPlus.md). Native smoke passes all profiles on the Windows desktop.)*
67. [x] transparent Clustered Forward+. *(2026-09-23: `FlagAlphaBlend` bin, GPU back-to-front sort `ForwardTransparentSort.slang`, premultiplied blending.)*
68. [x] cluster heatmap/overflow diagnostics. *(2026-09-23: `ClusterStats` and `RecordHeatmap`; done ahead of 66–67 because it validates assignment.)*
69. [x] 1k/10k+ light benchmarks. *(2026-09-23: CPU scaling scenarios + native `ClusteredLightingScalesToTensOfThousandsOfLights` with GPU pass timings; budgets are recorded from the desktop run.)*

### 35.7 Complete modern frame

70. [x] directional shadows. *(2026-09-23: `Renderer/Shadows` cascades + `Shaders/Slang/Shadows`; [Shadows](Shadows.md). Native smoke passes all profiles on the Windows desktop.)*
71. [x] spot atlas. *(2026-09-23: `ShadowAtlasAllocator` + `ShadowPlanner` budgets.)*
72. [x] point shadow policy. *(2026-09-23: six cube-face tiles under `MaxPointShadows` and the atlas policy.)*
73. [x] HDR scene color/exposure/tone mapping. *(2026-09-23: `Renderer/PostProcess` + `Shaders/Slang/PostProcess`; [Post-processing](PostProcess.md). Native smoke passes all profiles on the Windows desktop.)*
74. [x] bloom/color grading. *(2026-09-23: bloom chains and the grading stage of the composite.)*
75. [x] motion vectors/TAA. *(2026-09-24: Forward+ velocity target and jitter, `Renderer/Temporal` + `Shaders/Slang/Temporal`; [Temporal anti-aliasing](TemporalAntiAliasing.md). Native smoke passes all four profiles on the RTX 4070.)*
76. [x] optional AO/SSR/fog modules. *(2026-09-24: GTAO, SSR and height fog — Forward+ normal, indirect, reflectance and specular targets, `Renderer/ScreenSpace` + `Shaders/Slang/ScreenSpace`; [Screen-space effects](ScreenSpace.md). Native smokes pass all four profiles on the RTX 4070 and on Mesa lavapipe.)*
77. [x] GPU particles. *(2026-09-24: `Renderer/Particles` + `Shaders/Slang/Particles`; [GPU particles](Particles.md). Mesh/trail rendering and emitter assets remain Phase 19 follow-ups. Native smoke passes all four profiles on the RTX 4070 and on Mesa lavapipe.)*
78. [x] animation/skinning/morphs. *(2026-09-24: glTF skins/animations/morphs, `.sasset` skeleton and clip types, `Systems/Animation`, `Renderer/Skinning` + `Shaders/Slang/Skinning`, Forward+ previous-position motion vectors; [Animation and skinning](Animation.md). Native smoke passes all four profiles on the RTX 4070 and on Mesa lavapipe; 64 × 1,032-vertex skinning 0.054–0.074 ms. Engine wiring remains item 56.)*
79. [ ] runtime UI + HarfBuzz/FreeType/MSDF. *(2026-09-24: font faces, horizontal run shaping, bounded MSDF atlas pages and retained UI layout/paint/input foundation implemented; 20 focused cases pass. RHI quad rendering/atlas uploads, full paragraph text handling, editable widgets and sandbox migration remain. See [Text and retained UI](TextAndUi.md) and Phase 20 checkoffs.)*
80. [ ] miniaudio audio system.
81. [ ] `.spack` streaming/residency budgets/eviction.

### 35.8 Compatibility and hardening

82. [ ] Move OpenGL under legacy module and make it consume Platform/Input abstractions.
83. [ ] Remove Vulkan/OpenGL behavior from generic Vertex/Texture/Transform/Camera types.
84. [x] Retire external-editor IPC and disconnected scene-JSON synchronization outside the active tree. *(Archived under `Deprecated/`; future in-process tooling is separate work, not reactivation of this transport.)*
85. [ ] Move gizmos/editor camera to tooling/debug module.
86. [ ] Windows/Linux long-run/perf/validation hardening.
87. [ ] external engine consumer/API examples.
88. [ ] prepare Apple/Android platform/RHI/backend seams without implementing speculative platform code early.

---

## 36. Gates that prevent doing work in the wrong order

Before starting **RHI/Vulkan modernization**:

- [x] generic Window abstraction exists;
- [x] no RHI API takes `HWND`;
- [x] runtime graphics backend selection exists;
- [x] generic PCH no longer imports Vulkan/Win32 globally.

Before starting **GPU Scene**:

- [ ] live scene construction is explicit rather than static preregistration;
- [ ] persistent entity/asset identity is separate from runtime EnTT handles/source paths;
- [ ] AssetHandle exists;
- [ ] mesh/material are independent;
- [ ] scene Transform dirty state is not process-global;
- [ ] render extraction boundary exists.

Before starting **HZB/occlusion**:

- [x] canonical depth convention is final; *(2026-09-23, `DepthConvention.h`)*
- [x] reverse-Z decision is final; *(reverse-Z, infinite-far perspective)*
- [x] per-view history/camera-cut state exists. *(each `GpuVisibility` owns one view's LOD/occlusion history; `GpuViewFlags::CameraCut`)*

Before starting **Clustered Forward+**:

- [ ] GPU Scene is persistent;
- [x] GpuLight schema exists; *(`GpuLightRecord`/`GpuLightBuffer`, item 63)*
- [ ] Slang reflection/material binding is stable;
- [ ] PBR baseline works;
- [ ] RenderGraph is authoritative.

Before expanding **physics gameplay**:

- [x] Rigidbody contains no PhysX/Jolt pointer;
- [x] PhysX/Jolt parity baseline is registered through the same generic API. *(Both backend suites call the shared `PhysicsBackendContract`; configurations compile/run the cases only when the corresponding backend target exists.)*

Before expanding **asset streaming**:

- [x] runtime consumes `.sasset/.spack` identities, not source filenames; *(for the modern residency path: `AssetResidencyService` resolves `AssetId`s to cooked objects; `.spack` is later)*
- [x] async IO exists;
- [ ] GPU residency has deferred lifetime and budget tracking.

---

## 37. What not to do

- [ ] Do not use CMake as a dumping ground that globally links every dependency to the engine.
- [ ] Do not encode runtime backend selection as mutually exclusive build logic when multiple compiled backends can coexist.
- [ ] Do not build a new Vulkan renderer on top of `HWND` and promise to abstract the window later.
- [ ] Do not let SDL types become engine-wide public types simply because SDL solves the platform work.
- [ ] Do not keep `SwimEngine::GetInstance()` as the convenient answer to dependency wiring.
- [ ] Do not use static initialization to construct live scenes that later require Engine service injection.
- [ ] Do not serialize raw EnTT entity values or pool-derived file/name lookups as durable scene identity.
- [ ] Do not make one class responsible for scene serialization, filesystem persistence, and editor IPC.
- [ ] Do not grow the current `Renderer` facade until every backend fits; separate RHI, high-level rendering, asset residency, and UI policy instead.
- [ ] Do not rename `MeshPool`/`TexturePool` singletons and call the ownership problem solved.
- [ ] Do not keep material owning mesh just because GLB import currently creates them together.
- [ ] Do not let `Texture2D` constructors perform disk IO and GPU upload.
- [ ] Do not let Camera or Transform branch on Vulkan/OpenGL/D3D12/Metal.
- [ ] Do not make OpenGL implement an awkward fake explicit RHI purely for parity.
- [ ] Do not build descriptor/pipeline layout APIs before Slang reflection requirements are understood.
- [ ] Do not read GPU visibility results back to CPU for normal draw submission.
- [ ] Do not use per-object descriptor sets.
- [ ] Do not use device idle as routine lifetime management.
- [ ] Do not make mesh shaders mandatory.
- [ ] Do not build source glTF parsing into shipping model instantiation.
- [ ] Do not embed permanent GPU addresses/heap offsets into portable assets.
- [ ] Do not create a second renderer-specific thread pool after adding general Jobs.
- [ ] Do not tie runtime UI to scene transform Z hacks.
- [ ] Do not delete useful editor/debug functionality when it can simply be isolated from runtime architecture.

---

## 38. Definition of done

Swim Engine reaches the intended architecture when:

- [ ] CMake target boundaries match the engine dependency graph and generic targets do not transitively expose backend/importer libraries.
- [ ] compiled backend availability and runtime backend selection are separate concerns.
- [ ] Windows and Linux are first-class runtime platforms.
- [ ] generic engine code does not include Win32 types.
- [ ] Platform owns windows/events/native-handle bridging.
- [ ] gameplay consumes platform-neutral Input.
- [ ] engine services have explicit ownership and dependencies.
- [ ] scene creation/registration has deterministic explicit ownership rather than live static preregistration.
- [ ] scene persistence uses stable entity IDs and `AssetId` references and is independent from editor transport.
- [ ] no global engine singleton is needed for normal operation.
- [ ] graphics backend is runtime selectable.
- [ ] Vulkan is a clean production RHI backend.
- [ ] RHI can host D3D12/Metal without renderer surgery.
- [ ] OpenGL remains functional as isolated legacy compatibility.
- [ ] Slang is the normal first-party shader source/reflection pipeline.
- [x] source import is tool-side and fastgltf-based. *(The `SwimAssetCompiler` importer owns fastgltf privately and emits a Swim-owned intermediate representation.)*
- [x] compiled runtime assets are versioned, upload-friendly, and streamable. *(`.sasset` v1 is versioned/chunked/hashed/aligned with dependency and range-addressable chunk metadata; GPU residency/upload is intentionally a later phase.)*
- [x] mesh/material/texture identity is clean and non-singleton. *(Typed independent identities live in engine-owned `AssetSystem`; legacy renderer pools remain migration adapters, not the identity model.)*
- [ ] GPU Scene is persistent and independent from EnTT.
- [ ] visibility and draw generation are GPU-driven with no CPU feedback requirement.
- [x] GeometryHeap and bindless resources use safe deferred lifetimes. *(Registry-based timeline retirement for GeometryHeap, TextureResidency, bindless elements and cached samplers; the bindless native smoke still needs desktop runs.)*
- [ ] metallic-roughness PBR + IBL is production baseline.
- [ ] Clustered Forward+ is the standard local-light path.
- [x] shadows/HDR/post are RenderGraph modules. *(`Renderer/Shadows`, `Renderer/PostProcess` and `Renderer/Temporal` record graph passes only; the engine runtime constructs them with item 56.)*
- [x] PhysX and Jolt both implement the generic physics API and are runtime selectable.
- [ ] animation, particles, text/UI, audio, and streaming use the same jobs/assets/platform foundations rather than inventing new ones.
- [ ] editor/tooling systems can be wired in without changing runtime architecture.
- [ ] profiling, validation, and regression tests cover the performance-critical paths.

---

# Appendix A — Recommended dependency direction by module

```text
Core
  ^
  |
Platform <---- Input
  ^            ^
  |            |
Jobs <------- IO
  ^            ^
  |            |
Assets --------+
  ^
  |
Scene <----- Physics API
  ^             ^
  |             |
Render        Physics Backends
  ^
  |
RHI
  ^
  |
RhiVulkan
```

A more precise rule than the diagram is: **implementation modules may depend inward/downward, but public data contracts must not import implementation-layer types outward/upward.**

---

# Appendix B — Initial API sketches

These are direction-setting sketches, not frozen ABI.

## B.1 Engine startup

```cpp
#include <Swim/Engine/Engine.h>

int main(int argc, char** argv)
{
    Swim::EngineConfig Config;
    Config.Graphics = Swim::GraphicsBackend::Vulkan;
    Config.Physics = Swim::PhysicsBackend::Jolt;
    Config.Window.Title = "Swim";
    Config.Window.Width = 1920;
    Config.Window.Height = 1080;

    Swim::Engine Engine(Config);

    while (Engine.Tick())
    {
        // Game logic.
    }

    return 0;
}
```

## B.2 Assets

```cpp
AssetHandle<ModelAsset> Environment = Assets.Load<ModelAsset>(EnvironmentAssetId);
AssetHandle<TextureAsset> Albedo = Assets.Load<TextureAsset>(AlbedoAssetId);
```

## B.3 Render object without EnTT

```cpp
RenderObjectHandle Object = Renderer.Scene().CreateObject();
Renderer.Scene().SetMesh(Object, Mesh);
Renderer.Scene().SetMaterialSet(Object, Materials);
Renderer.Scene().SetTransform(Object, Transform);
```

## B.4 Generic physics

```cpp
PhysicsWorld& World = Physics.CreateWorld(WorldDesc);
BodyHandle Body = World.CreateBody(BodyDesc);
World.AddImpulse(Body, Impulse);
```

No API above identifies PhysX or Jolt.

---

# Appendix C — External technology references

These are implementation references, not public engine dependencies.

- SDL3: https://github.com/libsdl-org/SDL
- EnTT: https://github.com/skypjack/entt
- enkiTS: https://github.com/dougbinks/enkiTS
- fastgltf: https://github.com/spnda/fastgltf
- meshoptimizer: https://github.com/zeux/meshoptimizer
- KTX-Software: https://github.com/KhronosGroup/KTX-Software
- Slang: https://github.com/shader-slang/slang
- volk: https://github.com/zeux/volk
- vk-bootstrap: https://github.com/charles-lunarg/vk-bootstrap
- Vulkan Memory Allocator: https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
- PhysX: https://github.com/NVIDIA-Omniverse/PhysX
- Jolt Physics: https://github.com/jrouwe/JoltPhysics
- miniaudio: https://github.com/mackron/miniaudio
- FreeType: https://freetype.org/
- HarfBuzz: https://harfbuzz.github.io/
- msdf-atlas-gen: https://github.com/Chlumsky/msdf-atlas-gen
- Tracy: https://github.com/wolfpld/tracy
