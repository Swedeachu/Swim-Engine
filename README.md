# Swim Engine

This engine is built using **EnTT**, a custom **Scene System**, a first-party cross-platform **Platform/Input foundation**, and currently retains both **Vulkan** and **OpenGL** rendering backends while the renderer is being modernized.
The project is my life's work, nearly all knowledge in engineering I have goes into this in one way or another.
<br>
<br>
<img width="1260" height="583" alt="image" src="https://github.com/user-attachments/assets/b4a0f02d-65f6-4f38-b40c-8fc865500420" />

---

## Building

CMake is the only build-system source of truth. The repository does not commit generated Visual Studio projects or third-party source/binary trees. Dependencies are pinned and resolved with CPM.cmake. Downloaded dependency sources are shared across presets in `.cache/cpm`, while per-configuration build state remains under `build/<preset>`.

### Prerequisites

- CMake 3.25 or newer.
- Visual Studio 2022 with the Desktop development with C++ workload for the full Windows runtime.
- Git and Python 3. Python is used by GLAD and by PhysX's upstream project bootstrap.
- Ninja is optional on Windows: the build scripts discover Ninja from PATH or the Visual Studio CMake tools install and fall back to the Visual Studio generator when necessary.
- A Vulkan SDK for the legacy renderer's Vulkan headers/loader. The shader pipeline no longer needs `dxc.exe`: first-party shaders are Slang, and CMake downloads a pinned `slangc` SDK automatically. The modern RHI backend pins its own Vulkan headers, so it does not depend on the installed SDK version.
- On Linux, a C++20 compiler plus Ninja is sufficient for the current Platform/Input foundation build; the legacy renderer/game executable remains Windows-only until the later renderer/RHI phases are completed.

SDL3 is fetched and built by CMake as a pinned CPM dependency; no system-installed SDL3 is required. `Source/Engine/Platform` owns SDL3 usage; consuming executables link SDL3 privately, and public engine headers remain SDL-free.

### Visual Studio solution

Generate a normal Visual Studio 2022 solution with:

```powershell
cmake --preset windows-vs
```

Then open `build/windows-vs/SwimEngine.sln`. The solution is generated from the same `CMakeLists.txt` used by every other workflow. Solution Explorer mirrors the physical `Source/...` tree inside each project, and adding/removing/renaming C/C++ or shader files under `Source/Engine`, `Source/Game`, or `Source/Shaders` is picked up by the explicit CMake configure performed at the start of every supported clean/soft build.

**The IDE never regenerates the project itself.** `CMAKE_SUPPRESS_REGENERATION` is forced on for the Visual Studio generator as well as Ninja. Without it CMake attaches a stamp-check custom build to *every* project, and because MSBuild builds projects in parallel, a stale stamp starts one concurrent CMake configure per project against the same build tree — which corrupts generated files and races on the shared dependency caches (typically surfacing as `could not lock config file .git/config`, then a cascade of `MSB8066` and a `LNK1181` for a library that was never produced).

So after editing any `CMakeLists.txt` or `cmake/*.cmake`, re-run a build script or `cmake --preset windows-vs` before building in the IDE. Both build scripts refresh the solution on every run, so the normal loop already does this for you.

The generated solution is intentionally organized instead of exposing every CMake target at the root:

```text
SwimEngine                 # engine sources, enabled backends, and game code
Tests/
  SwimTests                # the entire runnable test corpus, one program
  Header Boundary/         # per-module public-header compile gates
Tools/
  SwimAssetCompiler        # asset compiler module library
  SwimAssetCooker          # the same module's command-line front end
Examples/
  SwimHelloWindow
  SwimHeadlessPlatform
Third Party/
  SDL3/
  Draco/
  WebP/
  Zstd/
  Basis Universal/
  GLAD/
  PhysX/
CMake/
  ALL_BUILD, ZERO_CHECK, INSTALL, ...
```

Engine sources compile directly into `SwimEngine`; source filters mirror the directories on disk. Tests and examples compile the source lists they need and link their private SDK dependencies. Each final binary gets each source once, including the asset-compiler ownership case documented in [Visual Studio project structure](docs/VisualStudioProjectStructure.md). All runnable tests live in `SwimTests`. Tools, examples, public-header compile gates, and third-party dependencies remain separate targets grouped by purpose.

Build either configuration from the terminal with:

```powershell
cmake --build --preset windows-vs
# or, for the legacy Debug + PhysX Checked configuration:
cmake --build --preset windows-vs-debug
```

The generated executable keeps the historical name `Swim Engine.exe`.

### Clean and soft terminal builds

A **clean build** is a full repository-local generated-state reset. It removes both Windows (or both Linux) configuration trees, the shared `build/.px` PhysX worktree/legacy junction, and the complete `.cache` dependency cache before configuring with dependency fetching enabled. The scripts verify those paths are actually gone before the first dependency is pulled. On Windows a clean build also regenerates `build/windows-vs/SwimEngine.sln` every time, even when Ninja is used for the actual compile, and verifies that the expected organized solution folders were emitted. Use a clean build when bootstrapping a checkout, intentionally refreshing every pinned dependency, or recovering from dependency/build corruption; ordinary C++/CMake iteration should use the soft build.

```powershell
# Windows Release / Debug
scripts\build-windows-clean.ps1
scripts\build-windows-clean.ps1 -Debug
```

```bash
# Linux Release / Debug foundation builds
./scripts/build-linux-clean.sh
./scripts/build-linux-clean.sh --debug
```

A **soft build** reconfigures with `FETCHCONTENT_FULLY_DISCONNECTED=ON` and reuses the existing `.cache/cpm` dependency sources. On Windows it also refreshes and validates the organized `build/windows-vs/SwimEngine.sln` from the same disconnected cache before running the fast Ninja/MSVC build, so Visual Studio stays synchronized with source files, target membership, and solution-folder changes during normal iteration. It will fail rather than downloading a missing dependency, which keeps normal iteration deterministic and fast.

```powershell
# Windows Release / Debug
scripts\build-windows-soft.ps1
scripts\build-windows-soft.ps1 -Debug
```

```bash
# Linux Release / Debug foundation builds
./scripts/build-linux-soft.sh
./scripts/build-linux-soft.sh --debug
```

For one-click use from Windows Explorer, matching `.bat` launchers are provided for all four workflows:

```text
scripts\build-windows-clean.bat
scripts\build-windows-soft.bat
scripts\build-linux-clean.bat
scripts\build-linux-soft.bat
```

The Windows launchers invoke the PowerShell scripts directly. The Linux launchers invoke the Bash scripts through WSL. All four preserve the underlying build exit code and always pause before closing so success or failure output remains visible. Any normal script arguments can still be appended when launching from a terminal, such as `build-windows-clean.bat -Debug` or `build-linux-clean.bat --debug`.

`build-windows.ps1` remains as a compatibility alias for the Windows soft-build path. The Windows scripts do not require a Developer Command Prompt: they discover standalone or Visual Studio-bundled CMake, locate Visual Studio 2022/Build Tools, import the x64 MSVC environment when using Ninja, discover Visual Studio's bundled Ninja even when it is not on `PATH`, and fall back to the Visual Studio generator if Ninja is unavailable. The helper intentionally uses `DebugBuild` internally rather than PowerShell's reserved/common `Debug` parameter name, while the public scripts retain the convenient `-Debug` switch. A normal Explorer double-click is therefore sufficient once Visual Studio 2022/Build Tools with Desktop development with C++ is installed. Both Windows clean and soft launchers leave `build/windows-vs/SwimEngine.sln` synchronized with the current CMake project. Clean recreates it after a full dependency reset; soft refreshes it offline from the existing validated cache before completing the requested Debug/Release Ninja build.

### Testing

Every runnable test in the engine is compiled into a single program, `SwimTests`, and both the Windows and Linux clean/soft build scripts build and run the whole suite. There are no per-module test binaries to remember.

```powershell
build\windows-release\SwimTests.exe                  # run everything
build\windows-release\SwimTests.exe --list           # list case identifiers
build\windows-release\SwimTests.exe --filter=Physics # run one area
build\windows-release\SwimTests.exe --help           # every option
```

Cases are identified as `<suite>.<name>`, for example `Assets.AssetSystem.UnloadFailAndForgetTransitions`. Filters accept `*`/`?` globs, and a filter without wildcards also matches by dotted prefix, so `--filter=AssetCompiler` selects every case under every `AssetCompiler.*` suite. Other options include `--exclude`, `--list-suites`, `--repeat`, `--shuffle[=seed]`, `--stop-on-failure`, `--verbose`, and `--report=<path>` for JUnit XML. The process exits non-zero if any selected case fails, or if the filter selected nothing.

Adding coverage does not touch the build system. Test cases self-register, so a new `.cpp` under `Source/Tests/Suites/<group>/` is picked up by the next configure:

```cpp
#include "Engine/Assets/AssetSystem.h"
#include "Tests/Framework/Test.h"

SWIM_TEST("Assets.AssetSystem", "UnloadKeepsIdentity")
{
    Swim::Assets::AssetSystem assets;
    SWIM_REQUIRE(assets.Initialize());
    // SWIM_CHECK / SWIM_CHECK_EQUAL / SWIM_CHECK_NEAR / SWIM_CHECK_THROWS ...
    assets.Shutdown();
}
```

The group directory decides which configurations compile the suite: `Core`, `Memory`, `Jobs`, `IO`, `Input`, `Assets`, `Physics/Generic`, and `Scene/Headless` build everywhere, while `AssetCompiler`, `Scene/Ecs`, and `Physics/PhysX` build only where those dependency targets exist. A Linux foundation build therefore runs the portable suites and omits the renderer/PhysX ones.

Note that Swim defines `NDEBUG` in every configuration, including Debug, so `assert()` is a no-op throughout the project. Test code must use the `SWIM_CHECK*`/`SWIM_REQUIRE*` macros, which always evaluate and always report.

A handful of small `OBJECT` libraries under the `Tests/Header Boundary` solution folder stay separate from `SwimTests` on purpose: each compiles a public-header surface with only its declared include paths/dependencies, proving those headers are self-contained. The broad dependency environment of `SwimTests` cannot prove that isolation.

### Vulkan RHI desktop validation

Build Debug `SwimTests` with `SWIM_ENABLE_VULKAN_RHI=ON` and `SWIM_BUILD_SHADER_COMPILER=ON` (both defaults). On a desktop with the required Vulkan feature baseline and validation layers, opt in to the seventeen native tests covering clear/transfer/presentation, triangle and texture readback, window/HDR lifecycle, timestamps, memory budgets, pipeline caches, upload/readback arenas, vertex/index/instance buffer drawing, push-constant updates, compute/storage-buffer readback, typed storage-image readback, entry-point descriptor readback, fixed descriptor-array readback and sampled integer texture readback:

```powershell
$env:SWIM_RUN_RHI_SMOKE = "1"
.\build\windows-debug\SwimTests.exe --filter=RHI.Vulkan.Smoke
Remove-Item Env:SWIM_RUN_RHI_SMOKE
```

```bash
SWIM_RUN_RHI_SMOKE=1 ./build/linux-debug/SwimTests --filter=RHI.Vulkan.Smoke
```

The lifecycle test needs a window manager that supports minimize/restore. The timestamp test requires at least one timestamp-capable graphics/compute family, exercises eight reset/write/readback cycles on each supported queue role, and reports unsupported roles. Dedicated transfer-only families currently cannot provide the GPU query-reset lifecycle. Missing video/GPU support fails the opted-in cases; default tests include dispatch-capture and frame-lifecycle coverage without a GPU. Each smoke explicitly requires active validation and fails on captured warnings, errors, or dropped diagnostics, including resource/device/instance teardown. Its report includes adapter and driver information. Debug regions and native object names are available to GPU tools when debug utils is supported. Cross-platform desktop evidence remains open in [the architecture plan](docs/SwimEngineArchitectureImplementationPlan.md).

To request synchronization or GPU-assisted checks, use the existing smoke runner with a validation profile:

```powershell
$env:SWIM_RUN_RHI_SMOKE = "1"
$env:SWIM_RHI_VALIDATION = "sync" # core, sync, gpu, or all
.\build\windows-debug\SwimTests.exe --filter=RHI.Vulkan.Smoke
Remove-Item Env:SWIM_RHI_VALIDATION
Remove-Item Env:SWIM_RUN_RHI_SMOKE
```

```bash
SWIM_RUN_RHI_SMOKE=1 SWIM_RHI_VALIDATION=sync ./build/linux-debug/SwimTests --filter=RHI.Vulkan.Smoke
```

`core` is the default. `sync` enables synchronization and submit-time checks; `gpu` enables GPU-assisted validation; `all` requests both. Core validation remains enabled in every smoke profile. Explicit checks require `VK_EXT_layer_settings` and a Khronos validation layer reporting API 1.4.335 or newer. GPU-assisted selection also requires vertex/fragment shader stores and atomics; normal core runs retain the existing device requirements. Missing support or an invalid profile fails the opted-in run. Run separate profiles first; instrumentation can significantly increase execution time.

Runtime callers set `GraphicsSystemDesc::Checks.Synchronization` and/or `GpuAssisted`. Explicit checks require validation/capture even with Default or IfAvailable mode; combining them with Disabled is invalid. `GetValidationConfiguration()` reports the backend's submitted configuration. External Vulkan Configurator/environment/file settings can override it, so keep acceptance runs free of conflicting overrides and inspect the diagnostics through teardown. The [architecture plan](docs/SwimEngineArchitectureImplementationPlan.md) records the remaining native validation gate. `SWIM_REQUIRE_HDR_SMOKE=1` separately requires HDR support in the HDR lifecycle test.

### Vulkan RHI vertex input

Set `GraphicsPipelineDesc::VertexBindings` and `VertexAttributes` to declare buffer slots, strides, vertex/instance rates and shader locations. Pipeline creation copies these spans. Bind `BufferUsage::Vertex` buffers with `BindVertexBuffer(slot, buffer, offset)` before drawing; empty layouts continue to support procedural shaders. The device reports layout limits through `GraphicsCapabilities::VertexInput`.

Attribute offsets, strides and bound offsets must satisfy component alignment, and attributes must fit a nonzero stride. Zero stride repeats one record. Direct draws validate vertex and instance byte ranges; indexed draws validate index bytes and instance ranges, while effective vertex indices remain the caller's responsibility. Keep buffers alive through GPU completion and issue explicit barriers. Shader locations and numeric types must match the declared attributes. See the explicit vertex-input checkpoint in [the architecture plan](docs/SwimEngineArchitectureImplementationPlan.md) for the complete contract and pending desktop validation.

### Vulkan RHI push constants

Global Slang `[[vk::push_constant]] ConstantBuffer<T>` blocks now convert through `BuildRhiShaderInterface` into reflected pipeline-layout ranges. After binding a graphics pipeline, call `CommandList::PushConstants(stages, offset, bytes)` using the reflected stage mask. Data is copied during recording; full or partial writes can occur inside or outside rendering. Writes need four-byte alignment and complete reflected stage coverage.

Initialize every reflected range byte before drawing, including padding. Values survive rendering-scope changes and pipeline switches with identical push-constant ranges. Binding an incompatible pipeline alone preserves them; a push under an incompatible layout requires full initialization of that layout before drawing. Pool reuse resets initialization. The current consumer supports vertex/fragment graphics stages and one global reflected block. See the push-constant checkpoint in [the architecture plan](docs/SwimEngineArchitectureImplementationPlan.md) for layout, lifetime and desktop-validation details.

### Development asset cooking

The Phase 4 development asset path treats loose `.gltf`/`.glb` files as authoring inputs and `.sasset` files as the runtime representation. With `SWIM_ENABLE_DEV_ASSET_AUTOCOOK=ON` (the development default), engine startup scans the platform asset root, skips current cooked roots, and recooks missing/stale/corrupt models through fastgltf -> source codecs (Draco/WebP/KTX2 as required) -> meshoptimizer -> the static-model `.sasset` compiler before loading the cooked dependency graph into `AssetSystem`.

Cooked files mirror the source tree under `Assets/Cooked`; dependency objects are content-validated files under `Assets/Cooked/.objects`. `Assets/Cooked/` is generated build output rather than authoring content, so it is Git-ignored: only the source tree under `Assets/` is committed, and any checkout regenerates the cooked graph on first run or with `SwimAssetCooker`. Local external glTF dependencies such as `.bin` files are part of the source fingerprint, so editing one invalidates the matching model even when the `.gltf` file itself did not change. PNG/JPEG/WebP are decoded and mip-generated on the compiler side; already-KTX2/Basis payloads are preserved through the current compiler boundary. Final platform-native/KTX2 compression policy is still in progress.

The asset compiler and the cooker are one module with two build outputs: `SwimAssetCompiler` is the library, and `SwimAssetCooker` is its command-line front end at `Source/Tools/AssetCompiler/Cli/AssetCookerMain.cpp`. The separate CMake target exists only because a static library cannot own a `main()`; there is one cook implementation, shared by the tool and by engine-start auto-cook.

The same path can be run without launching the engine:

```powershell
# After configuring/building the asset-compiler targets
build\windows-release\SwimAssetCooker.exe Assets
```

The asset compiler owns pinned simdjson/fastgltf/meshoptimizer/Draco/libwebp dependencies through `Swim::AssetCompilerDependencies`. Draco 1.5.7 is wrapped by `Swim::AssetCompilerDraco`, which supplies both its `<source>/src` headers and generated `draco/draco_features.h` include root to compiler/test consumers while keeping that package-layout quirk out of first-party source. The first Windows build after adding or changing this checkpoint should use the clean build once to populate `.cache/cpm`; subsequent normal iteration can use the soft build again.

For a shipping/runtime-only configuration, set `SWIM_ENABLE_DEV_ASSET_AUTOCOOK=OFF`; the runtime `.sasset` reader remains in `Swim::Assets`, while fastgltf, Draco, libwebp, and meshoptimizer stay on the compiler side. Basis Universal is the intentional transitional exception: only `Swim::BasisTranscoder` remains runtime-facing while universal KTX2/Basis payloads are transcoded at residency time.

### Dependency policy

The previous `Source/Library` copies are replaced by pinned/verified CMake dependencies. Runtime ownership is SDL3, mimalloc, enkiTS, spdlog, GLM, EnTT, nlohmann/json, transitional stb compatibility, zstd, the Basis transcoder, GLAD/OpenGL, Vulkan, and PhysX. Asset-compiler-only ownership is simdjson/fastgltf, meshoptimizer, Draco, libwebp, and compiler-side stb. tinygltf is retired. Nothing downloaded by CMake should be committed, and codec support does not imply that codec belongs in the shipping runtime.

PhysX is kept deliberately isolated because its configuration model does not match the application's Debug/Release model. The pinned PhysX 5.6.1 checkout in CPM is treated as immutable. At build time Swim creates a short detached Git worktree at `build/.px`; NVIDIA's generated `compiler/` and `bin/` trees live there, keeping both MSBuild paths short and the CPM source cache clean for later soft builds. Swim Engine Debug links the **Checked** static PhysX libraries, while Swim Engine Release links **Release** PhysX. Both are built with the static non-debug MSVC runtime, matching the previous x64 project configuration (`/MT`, `PX_PHYSX_STATIC_LIB`, Debug `_ITERATOR_DEBUG_LEVEL=0`). The CPU-only VS2022 preset is used, so CUDA is not required. CMake audits every Git-backed cached dependency at configure time and fails immediately if a dependency source checkout is dirty instead of allowing that state to surface as a later compile failure.

### Shaders

All first-party shaders are **Slang**. There is no handwritten HLSL or GLSL left in the build; the retired sources are archived under `Deprecated/Shaders/` and are not compiled, copied, or referenced.

CMake downloads a pinned, SHA-256-verified `slangc` SDK and compiles every shader deterministically, emitting reflection JSON beside each artifact:

```text
Source/Shaders/Vulkan/{Vertex,Fragment,Compute}Shaders/*.slang  ->  Shaders/<group>/<name>.spv   + .reflection.json
Source/Shaders/OpenGL/*.slang                                   ->  Shaders/OpenGL/<name>.glsl   + .reflection.json
```

Artifacts are produced as real CMake `OUTPUT`s with depfiles (not `PRE_BUILD` side effects), so incremental builds and dependency tracking work, and they are copied beside the executable after the build along with `Assets`. Reflection is read from the JSON sidecar rather than from decorations embedded in the SPIR-V, which keeps the emitted modules free of `SPV_GOOGLE_*` extensions that would otherwise require matching device extensions.

---

## Features

- **Entity Component System (ECS):** Scene management powered by EnTT with a Behavior component system for lifecycle-driven scripting.  
- **Model & Texture Loading:** Full **GLTF/GLB** pipeline with bindless texture support, mipmap generation, and multiple image formats.  
- **Rendering Abstraction:** Vulkan and OpenGL renderers with complete feature parity.  
- **Skybox System:** Cubemap rendering with adjustable rotation, exposure, and per-face textures.  
- **Spatial Partitioning:** Scene-level **BVH** for accelerated frustum culling and ray queries.  
- **GPU-Driven Rendering:** Vulkan bindless indexed indirect draw system for high-performance instancing.  
- **Text & SDF Rendering:** MSDF-based text rendering and stylized SDF effects (outline, color, softness).  
- **Debug Rendering:** Immediate-mode 3D debug mesh rendering.  
- **Physics Boundary:** Backend-neutral generational handles, descriptors, queries, collision/trigger events, and scene synchronization with the current PhysX implementation isolated behind `SwimPhysicsPhysX`.  
- **Input System:** Platform-neutral keyboard, mouse, text/IME, and gamepad events/state with action-map support; SDL3/native translation is isolated in the Platform implementation.

---

## Current Development Goals

- Editor gizmos and property inspectors for primitive component fields.  
- Archetype and prefab pipeline for Behavior components.  
- Deliberate scene persistence format/restore path after the runtime scene model stabilizes; the old automatic JSON sync experiment is currently dormant.  
- Jolt backend implementation and parity testing against the existing generic Physics/PhysX contract.  
- Compute-based culling pass + occlusion.  
- Recursive parent-child transform hierarchy for UI entities.  
- Controller input support.  
- In-process editor UI: hierarchy, inspectors, gizmos, asset/scene tooling, and debugging directly against engine state.

---

## Future Objectives

Once the current goals listed above are completed, development will shift toward advanced rendering and runtime systems:

- Physically Based Rendering (PBR)  
- Clustered Forward+ rendering pipeline with global illumination  
- Dynamic and baked shadow systems  
- GPU-driven particle simulation  
- Skeletal animation and ragdoll physics  
- MiniAudio integration for audio playback  
- Multithreaded file I/O and asynchronous scene streaming  
- Binary GPU buffer asset formats for optimized runtime loading

### Vulkan device-loss reports

Retain `Device::GetDeviceDiagnostics()` before starting GPU work. A Vulkan device-loss result raises `Rhi::DeviceLostError` from fallible work and preserves the first operation, native result and optional fault details in the retained snapshot. Stop/join GPU workers before releasing frames, resources and the device. Lost-device teardown makes one serialized idle attempt and retains its outcome; it does not automatically recreate the device.

`GraphicsSystemDesc::DeviceFaultDiagnostics` optionally enables supported `VK_EXT_device_fault` textual/address reports, with bounded data and no vendor binary dumps. Missing support is allowed. See the [architecture implementation plan](docs/SwimEngineArchitectureImplementationPlan.md) for the caller contract, validation evidence and remaining desktop checks.

### Vulkan memory telemetry

`Device::GetMemoryBudgetSnapshot()` returns owned per-heap allocator counters and memory-budget estimates without a GPU wait. Check `IsAvailable()` and each heap's `Source`: driver estimates describe process usage/budget; allocator fallback uses reserved block bytes and an 80%-of-capacity heuristic. `GetHeadroomBytes()` saturates at zero when usage exceeds budget. Host-visible and device-local heaps can overlap on UMA hardware.

The opt-in `RHI.Vulkan.Smoke.MemoryBudgetAllocationAndRelease` case checks real buffer/texture allocation and release with required validation. The [architecture implementation plan](docs/SwimEngineArchitectureImplementationPlan.md) documents counter semantics, sampling limits and the remaining desktop validation gate.

### Vulkan pipeline caches

Graphics and compute pipelines automatically share a native cache per device. Call `Device::LoadPipelineCache(bytes)` before the first pipeline build to seed it, then persist the complete `Bytes` from a `Ready` `Device::GetPipelineCacheData()` result at a caller-chosen checkpoint. Cache data is bounded, versioned and checked against the device/driver; invalid files permit normal cold compilation. The RHI performs no automatic filesystem writes.

The strict opt-in `RHI.Vulkan.Smoke.PipelineCachePersistenceAndReuse` case checks export, file round trip and recreation on a second device. See the [architecture implementation plan](docs/SwimEngineArchitectureImplementationPlan.md) for result handling, synchronization and persistence requirements.


### Persistent upload arenas

`Swim::Rhi::UploadArena` provides bounded staging storage backed by a persistently
mapped `CpuToGpu` buffer. Enable per-frame storage with
`FrameContextDesc::Upload.Capacity`, then use `FrameContextRing::AllocateUpload`
or `WriteUpload` between `BeginFrame` and `SubmitCurrent`. Slices contain the
buffer, byte offset and writable byte span. Submission flushes the used bytes;
frame-slot reuse waits for the timeline before reclaiming them.

Capacity exhaustion returns an empty optional. Finish CPU writes before
submission and keep slices within their owning frame. Standalone arenas require
explicit flushing and completion before reset/destruction. See the architecture
plan's upload-arena checkpoint for alignment, failure and lifetime contracts, and
`VulkanUploadArenaSmokeTests.cpp` for buffer/texture copies across reused slots.
The native test joins `SWIM_RUN_RHI_SMOKE=1 SwimTests --filter=RHI.Vulkan.Smoke`.


### Readback arenas

`Swim::Rhi::ReadbackArena` provides bounded, persistently mapped transfer destinations.
Allocate slices, record GPU copies and a final `HostRead` transition, then pass the
arenas to `FrameContextRing::SubmitCurrent`. The frame signal gates CPU reads and
retains buffers until completion. `TryRead` copies ready bytes; `TryGetData` returns
a const mapped view. Neither waits for the GPU.

Results survive frame-slot reuse and `Drain`. Call `TryReset` after consuming or
explicitly discarding them; it refuses to reclaim in-flight storage and invalidates
old slices on success. See the architecture guide's readback checkpoint and
`VulkanReadbackArenaSmokeTests.cpp` for the complete lifetime and transfer example.
The native test joins the existing `SWIM_RUN_RHI_SMOKE=1` smoke suite.

### Vulkan RHI compute

Compile a fixed-local-size compute entry with the pinned Slang compiler and convert its sidecar with `BuildRhiShaderInterface`. Pass all three owned interface members into `ShaderProgramDesc::Interface`: `DescriptorSchemas`, `PushConstants` and `ComputeThreadGroupSize`. Create its layout and compute pipeline, bind initialized descriptor tables and push constants, then call `Dispatch(x, y, z)` outside rendering on a compute-capable queue. Counts are workgroups; the device exposes limits through `GraphicsCapabilities::Compute`.

`RWStructuredBuffer` and `RWByteAddressBuffer` use `DescriptorType::StorageBuffer`. Dependent read/write passes require explicit `ShaderRead | ShaderWrite` barriers, including same-state barriers. Transition output for copies and readback for host access, then wait for completion. Keep each buffer on one queue family and retain all referenced resources until completion. Binding graphics or compute selects the active pipeline and requires rebinding tables. Push-constant range compatibility follows the same rules as graphics.

`VulkanComputeSmokeTests.cpp` demonstrates two dependent 3D dispatches, a partial constant update, guarded output elements and CPU verification across four frames. It joins `SWIM_RUN_RHI_SMOKE=1 SwimTests --filter=RHI.Vulkan.Smoke`. Typed 2D storage images are implemented below. Graphics-stage writes, indirect dispatch and queue ownership transfers remain separate work. See the architecture plan for scope and the open desktop validation gate.

### Typed storage textures

Compute programs can use explicitly formatted `RWTexture2D` resources, for example `[[vk::image_format("rgba32f")]] RWTexture2D<float4> Output;`. Reflection supplies `DescriptorBindingDesc::StorageTextureFormat`; create a matching image with `TextureUsage::Storage` and a 2D view covering one mip and one layer. Bind the view with `DescriptorWrite::TextureResource`. Float, unsigned and signed formats are supported as listed in the architecture plan; formatless, multisampled and other image shapes remain outside this contract.

Use `ShaderRead | ShaderWrite` for storage-image access in General layout. Add an explicit same-state barrier between dependent dispatches. Plain texture `ShaderRead` selects sampled-image access. Graphics and compute-capable families support color-image transfers and barriers, so upload, compute and readback can all stay on one family. Keep resources alive and wait for completion before CPU access; queue ownership transfers remain unsupported.

`VulkanStorageTextureSmokeTests.cpp` demonstrates typed float/uint/int images, nonzero mip/layer views, guarded writes, dependent passes and readback across reused frame slots. It joins the existing native smoke suite and requires desktop Vulkan validation support.

### Entry-point resource descriptors

Slang entry functions can declare directly bound `uniform` resources, for example `[[vk::binding(5, 1)]] uniform RWStructuredBuffer<uint> Output`. `BuildRhiShaderInterface` combines these with globals, preserves their absolute binding coordinates and gives entry descriptors only their declaring stage's visibility. Shared globals keep all program stages. Every duplicate `(space, binding)` rejects; independent declarations are not implicit aliases.

Use the returned descriptor schemas, push ranges and compute local size together when creating the RHI program. The existing descriptor-table and synchronization APIs apply. Writable buffers and typed storage images remain compute-only. The new `ScopedCompute.slang` smoke exercises a global input/push block with entry-local output and uniform buffers across two descriptor spaces; `ScopedGraphics.slang` verifies shared, vertex-only and fragment-only reflection.

Nested parameter blocks, implicit entry uniform containers, runtime-sized descriptor arrays and entry-local push-constant blocks still reject. For entry uniform data, use an explicitly bound `ConstantBuffer<Settings>`; plain `uniform uint` parameters introduce an unsupported container. Keep push constants in the supported global block. See the architecture plan's entry-point descriptor checkpoint for the exact scope and validation status.

### Fixed descriptor arrays

Slang global and direct entry resources can use one-dimensional fixed arrays, such as `[[vk::binding(2, 0)]] StructuredBuffer<uint> Inputs[2];`. Reflection produces one binding with `Count == 2`. Populate every element using `DescriptorWrite::ArrayIndex`; an incomplete table cannot be bound, and a recorded table is immutable. Arrays support the same six descriptor classes and image-format/stage restrictions as scalar bindings.

The `DescriptorArrays.slang` smoke uses both elements of all six classes and verifies buffer/image readback after dependent compute passes. Its descriptor indices are compile-time constants. Fixed array allocation does not enable dynamic/non-uniform indexing for all resource classes; that requires the corresponding device features. Runtime-sized/bindless arrays, partially bound descriptors, update-after-bind mutation and nested descriptor arrays remain separate work. See the architecture plan's fixed descriptor-array checkpoint for validation and scope.

### Typed sampled 2D textures

Slang `Texture2D<float/uint/int>` declarations now preserve their numeric class in
`DescriptorBindingDesc::SampledClass`, including fixed descriptor arrays and direct
entry-point resources. Descriptor writes reject a view with the wrong signedness or
numeric class. Uint/Sint refer to shader results, not the texture's channel count or
bit width: a `Texture2D<uint>` can read R8Uint or R32Uint. Float covers normalized,
sRGB and floating-point color formats. Depth/comparison and additional image shapes
remain separate work.

Use integer `Load` operations for exact texel reads without a sampler. Integer views
require native sampled-image support; they do not require linear-filter support.
Float views retain the existing filtering requirement. The RHI does not inspect
shader/sampler pairings, and enabling integer descriptors does not enable arbitrary
filtering or dynamic/non-uniform descriptor indexing features.

The opt-in `RHI.Vulkan.Smoke.SampledIntegerTexturesAndReadback` checks four frames of
unsigned/signed/narrow-integer/normalized inputs, fixed arrays, nonzero mip/layer
views, output guards and CPU readback. Run it through the desktop smoke command above.
The sandbox still uses the transitional Vulkan renderer; launching `Swim Engine.exe`
does not run this RHI validation suite. The architecture guide's current snapshot
records which foundations are active in the sandbox and which RHI consumers remain separate.
