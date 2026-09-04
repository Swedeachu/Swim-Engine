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
- A Vulkan SDK that provides Vulkan headers/libraries and `dxc.exe` for the existing Windows HLSL-to-SPIR-V shader build.
- On Linux, a C++20 compiler plus Ninja is sufficient for the current Platform/Input foundation build; the legacy renderer/game executable remains Windows-only until the later renderer/RHI phases are completed.

SDL3 is fetched and built by CMake as a pinned CPM dependency; no system-installed SDL3 is required. `Swim::Platform` owns SDL3 privately so SDL headers do not become public engine API.

### Visual Studio solution

Generate a normal Visual Studio 2022 solution with:

```powershell
cmake --preset windows-vs
```

Then open `build/windows-vs/SwimEngine.sln`. The solution is generated from the same `CMakeLists.txt` used by every other workflow. Solution Explorer mirrors the physical `Source/...` tree inside each project, and adding/removing/renaming C/C++ or shader files under `Source/Engine`, `Source/Game`, or `Source/Shaders` is picked up by the explicit CMake configure performed at the start of every supported clean/soft build.

The generated solution is intentionally organized instead of exposing every CMake target at the root:

```text
SwimEngine                 # primary executable; normal engine/game/renderer code
Engine Modules/
  SwimPlatform             # reusable platform boundary
  SwimInput                # reusable input boundary
Tests/
  SwimPlatformPublicHeaders
  SwimInputTests
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

`SwimPlatform` and `SwimInput` are real first-party module targets, not duplicate copies of `SwimEngine`: their source files are explicitly excluded from the `SwimEngine` source glob and linked into the executable once. Keeping those two foundational boundaries separate allows headless/tools/tests to reuse them without linking the whole renderer/game. Tests and examples stay visible for explicit validation but are `EXCLUDE_FROM_ALL`, so a normal engine build does not compile them. Third-party projects remain real dependency targets but are collapsed under `Third Party` (with large dependency graphs such as Draco and WebP nested again) rather than cluttering the solution root. CMake's predefined projects are kept under `CMake`.

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

### Development asset cooking

The Phase 4 development asset path treats loose `.gltf`/`.glb` files as authoring inputs and `.sasset` files as the runtime representation. With `SWIM_ENABLE_DEV_ASSET_AUTOCOOK=ON` (the development default), engine startup scans the platform asset root, skips current cooked roots, and recooks missing/stale/corrupt models through fastgltf -> source codecs (Draco/WebP/KTX2 as required) -> meshoptimizer -> the static-model `.sasset` compiler before loading the cooked dependency graph into `AssetSystem`.

Cooked files mirror the source tree under `Assets/Cooked`; dependency objects are content-validated files under `Assets/Cooked/.objects`. Local external glTF dependencies such as `.bin` files are part of the source fingerprint, so editing one invalidates the matching model even when the `.gltf` file itself did not change. PNG/JPEG/WebP are decoded and mip-generated on the compiler side; already-KTX2/Basis payloads are preserved through the current compiler boundary. Final platform-native/KTX2 compression policy is still in progress.

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

The existing Vulkan HLSL pipeline is also part of CMake: vertex, fragment, and compute shaders are compiled with DXC to SPIR-V under the executable's `Shaders` directory, and OpenGL shaders plus `Assets` are copied beside the executable after the build.

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
- **Input System:** Platform-neutral keyboard, mouse, text/IME, and gamepad events/state with action-map support; SDL3/native translation is isolated in the Platform implementation.

---

## Current Development Goals

- Editor gizmos and property inspectors for primitive component fields.  
- Archetype and prefab pipeline for Behavior components.  
- Full scene serialization and deserialization.  
- Physics integration with library abstraction (PhysX and Jolt).  
- Compute-based culling pass + occlusion.  
- Recursive parent-child transform hierarchy for UI entities.  
- Controller input support.  
- DLL + C# runtime bindings for external editor integration.

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
