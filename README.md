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

Build Debug `SwimTests` with `SWIM_ENABLE_VULKAN_RHI=ON` and `SWIM_BUILD_SHADER_COMPILER=ON` (both defaults). On a desktop with the required Vulkan feature baseline and validation layers, opt in to clear/transfer/presentation, triangle pixel/indexed parity, reflected texture readback, and resize/minimize/restore tests:

```powershell
$env:SWIM_RUN_RHI_SMOKE = "1"
.\build\windows-debug\SwimTests.exe --filter=RHI.Vulkan.Smoke
Remove-Item Env:SWIM_RUN_RHI_SMOKE
```

```bash
SWIM_RUN_RHI_SMOKE=1 ./build/linux-debug/SwimTests --filter=RHI.Vulkan.Smoke
```

The lifecycle test needs a window manager that supports minimize/restore. Missing video/GPU support fails the opted-in cases; default tests include dispatch-capture and frame-lifecycle coverage without a GPU. Each smoke explicitly requires active validation and fails on captured warnings, errors, or dropped diagnostics, including resource/device/instance teardown. Its report includes adapter and driver information. Debug regions and native object names are available to GPU tools when debug utils is supported. Cross-platform desktop evidence remains open in [the architecture plan](docs/SwimEngineArchitectureImplementationPlan.md).

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
