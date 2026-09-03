# Swim Engine — Modern Cross-Platform Architecture and Implementation Plan

> **Mission:** turn Swim Engine into a clean, general-purpose, cross-platform C++ engine whose low-level platform contracts are stable before higher-level systems are built, whose main renderer is a modern GPU-driven Vulkan renderer behind an explicit RHI, and whose major replaceable systems can be selected at runtime without leaking their implementation libraries into gameplay code.
>
> **Primary targets:** Windows and Linux are first-class now. macOS/iOS and Android are later targets that the architecture must make straightforward rather than requiring another foundational rewrite.
>
> **Rendering direction:** Vulkan is the primary modern graphics backend. D3D12 and Metal are future RHI backends. OpenGL remains a functional legacy renderer, but it must not constrain the modern RHI or require feature parity with the GPU-driven renderer.
>
> **Physics direction:** gameplay talks only to Swim physics contracts. PhysX and Jolt are selectable backend implementations.
>
> **Shader direction:** Slang is the source language/compiler stack for new first-party shaders. Vulkan consumes SPIR-V. Future D3D12 consumes DXIL. Metal support is added when the Slang/Metal path is sufficiently mature. Legacy OpenGL may consume Slang-generated GLSL/SPIR-V where practical.

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
- [ ] Asset identity and ownership are fixed before the new renderer starts storing mesh/material/texture references.
- [ ] Scene ownership and transform dirty tracking are fixed before GPU Scene extraction is implemented.
- [ ] Shader reflection contracts are defined before descriptor/pipeline layouts become entrenched in the RHI.
- [ ] The RHI contract is defined before Vulkan implementation details spread through new renderer code.
- [ ] Physics components contain backend-neutral handles before Jolt and PhysX coexist.
- [x] No new code reaches through `SwimEngine::GetInstance()` to discover dependencies.
- [x] No new public generic header includes Win32, Vulkan, OpenGL, PhysX, Jolt, SDL implementation details, or source-importer types.
- [ ] Source import is never the normal shipping runtime asset path.
- [ ] OpenGL compatibility never lowers the design of the modern RHI.

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

## 2. Current repository architecture audit

The repository already contains useful renderer, scene, BVH, text, behavior, physics, and indirect-drawing work. The modernization should preserve good algorithms and working behavior while correcting the dependency and ownership model around them.

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

### 2.10 Physics is only partially abstracted

`Rigidbody.h` avoids including PhysX headers but still stores `PxRigidActor*` and `PxShape*`. `PhysicsSystem.h` and `PhysicsWorld.h` expose PhysX APIs directly. Scene physics creation is tied to the concrete PhysX system.

**Target:** `BodyHandle`, `ShapeHandle`, `PhysicsMaterialHandle`, `ConstraintHandle`, query hit types, collision events, and world operations are Swim types. Backend objects live only in backend implementation storage.

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
- [ ] `Swim::RhiVulkan` is the only normal layer allowed to include Vulkan implementation types.
- [ ] `Swim::RhiD3D12` and `Swim::RhiMetal` can be added without changing high-level renderer contracts.
- [ ] `Swim::PhysicsPhysX` is the only normal layer allowed to include PhysX implementation types.
- [ ] `Swim::PhysicsJolt` is the only normal layer allowed to include Jolt implementation types.
- [x] SDL types do not become the public engine API. SDL is the Platform/Input implementation library.
- [ ] fastgltf types do not escape the asset importer/tool boundary.
- [ ] enkiTS types do not become gameplay APIs.
- [ ] Persisted scene references never use raw `entt::entity` values as durable identity.
- [ ] Scene serialization is independent from filesystem storage and editor/IPC transport.
- [ ] Static initialization does not construct live Scene instances or require Engine services.
- [ ] RHI contracts do not contain UI canvas policy or high-level environment features.
- [ ] Material objects do not own meshes.
- [ ] Mesh assets do not own backend GPU buffers.
- [ ] Texture assets do not own raw Vulkan/OpenGL objects.
- [ ] Constructors do not perform hidden disk IO or synchronous GPU uploads.
- [ ] Scene/ECS objects do not store raw RHI resources.
- [ ] Scene/ECS objects do not store PhysX/Jolt pointers.
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
| fastgltf | glTF/GLB source import | Use in asset compiler/dev importer; do not make it a shipping runtime dependency for compiled assets. |
| meshoptimizer | vertex/index optimization, LOD, meshlets | Use offline in asset compiler. |
| KTX-Software/libktx | KTX2 texture processing/transcoding | Use for compiled texture path. |
| zstd | package/chunk compression | Keep behind asset/package code. |
| Slang | shader language/compiler/reflection | Use for all new first-party shaders. |
| Vulkan-Headers | Vulkan API definitions | Use in Vulkan backend only. |
| volk | Vulkan dispatch loading | Use in Vulkan backend. |
| vk-bootstrap | instance/device/queue/swapchain bootstrap | Use for boilerplate, while Swim owns feature and adapter policy. |
| Vulkan Memory Allocator | Vulkan allocation/suballocation/budgeting | Use. Do not maintain a home-grown general Vulkan allocator. |
| PhysX | physics backend | Keep as a selectable implementation. |
| Jolt Physics | physics backend | Add as an equal selectable implementation. |
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
- fastgltf: tool/import boundary, no runtime wrapper object graph needed.
- meshoptimizer: call directly in the compiler implementation.
- VMA: keep inside Vulkan backend, not behind an additional allocator abstraction unless required by RHI internals.

### 4.2 Build graph and CMake invariants

CMake is the authoritative build description and should reinforce the engine architecture rather than merely collect source files. It is infrastructure used throughout every phase, not a separate feature milestone.

- [ ] First-party subsystems have explicit targets with intentional public/private dependency edges.
- [ ] Third-party libraries are linked only by the implementation target that owns them.
- [ ] `Swim::Rhi` does not link Vulkan; `Swim::RhiVulkan` owns Vulkan-Headers, volk, vk-bootstrap, and VMA.
- [ ] `Swim::Physics` does not link PhysX or Jolt directly; `Swim::PhysicsPhysX` and `Swim::PhysicsJolt` own those dependencies.
- [x] `Swim::Platform` / `Swim::Input` own SDL3 integration rather than making SDL3 an engine-wide dependency.
- [ ] fastgltf, meshoptimizer, Draco, source-image decoders, and similar importer dependencies live in asset compiler/import targets unless runtime use is explicitly required.
- [ ] Slang shader compilation/reflection is integrated through dedicated build/tool rules with correct source/include dependency tracking.
- [ ] Generated shader artifacts and compiled asset outputs have deterministic build dependencies and are not maintained by ad-hoc post-build shell scripts.
- [ ] Platform-specific source files and libraries are selected inside the appropriate implementation targets; generic code does not accumulate broad `#ifdef _WIN32` / Linux branches.
- [x] No machine-specific absolute include/library paths.
- [ ] Third-party compiler options and warnings do not leak into first-party targets.
- [x] Tests and examples link the same public Swim targets that real applications are expected to consume.
- [x] Visual Studio solution organization reflects ownership without inventing duplicate source ownership: `SwimEngine` stays at the root, first-party foundation modules are under `Engine Modules`, tests/examples are separated, dependencies are grouped under `Third Party`, and generated CMake projects are grouped under `CMake`.
- [x] Tests/examples are `EXCLUDE_FROM_ALL`, so validation/demo targets remain explicitly buildable without bloating the normal engine build.
- [x] Windows clean and soft workflows both regenerate and validate `build/windows-vs/SwimEngine.sln`; the soft path refreshes it with dependency fetching fully disconnected while the actual iterative compile remains on the Ninja `windows-release`/`windows-debug` tree.
- [x] `SwimPlatform` / `SwimInput` sources are excluded from the legacy `SwimEngine` source glob and linked once through their module targets rather than duplicated into the executable target.
- [ ] Optional implementation backends are compile-time capabilities; the selected implementation is a runtime choice among the backends that were compiled in.

**Build workflow checkpoint (2026-09-02, hardened 2026-09-03):** SDL3 is a pinned CPM dependency (`libsdl-org/SDL`, `release-3.4.14`) owned privately by `Swim::Platform`. CPM sources are cached under `.cache/cpm`. A clean build is now defined as a full repository-local generated-state reset rather than merely deleting the selected target: Windows clean removes `build/windows-release`, `build/windows-debug`, `build/windows-vs`, the shared `build/.px` PhysX worktree/legacy junction, and the entire `.cache` tree; Linux clean removes both Linux configuration trees, `build/.px`, and `.cache`. Both scripts verify that the generated state is actually absent before fetching. `scripts/build-windows-soft.ps1` and `scripts/build-linux-soft.sh` configure with `FETCHCONTENT_FULLY_DISCONNECTED=ON`, so iterative rebuilds are restricted to already-cached dependency sources. Windows and Linux Debug/Release Ninja presets exist for those scripts. The legacy `scripts/build-windows.ps1` now forwards to the soft-build path. Matching `.bat` launchers exist for all four Windows/Linux clean/soft workflows; the Linux launchers run the Bash scripts through WSL, and every launcher preserves the build exit code and pauses before closing so one-click builds remain readable. Windows builds are also self-bootstrapping with respect to the local toolchain environment: `scripts/windows-build-common.ps1` discovers CMake from PATH or Visual Studio, finds Visual Studio/Build Tools through `vswhere` or standard install locations, imports the x64 MSVC environment when Ninja is selected, discovers Visual Studio's bundled Ninja even when it is not on PATH, and falls back to the Visual Studio 2022 generator when Ninja is unavailable. The helper uses `DebugBuild` internally to avoid colliding with PowerShell's built-in common `Debug` parameter while the public scripts continue to accept `-Debug`. Every clean Windows run also recreates `build/windows-vs/SwimEngine.sln`; when Ninja is the primary compile generator, the solution configure is performed only after the primary Ninja build completes, using the freshly populated and integrity-checked dependency cache with dependency downloads disabled. Windows soft builds follow the same build-first ordering and perform the Visual Studio solution configure in fully disconnected mode on every successful run, so `build/windows-vs/SwimEngine.sln` stays synchronized without placing a second-generator configure between Ninja configure and compilation. Git-backed dependency caches are audited as immutable inputs during configure, and PhysX now builds from a short detached Git worktree at `build/.px` rather than a junction into the CPM checkout, so NVIDIA-generated compiler/bin output can no longer dirty the cached PhysX source. This removes the previous requirement to launch builds from a Developer Command Prompt or install Ninja separately while ensuring a normal Visual Studio solution is always available and synchronized after either Windows build workflow.


**Visual Studio solution hygiene checkpoint (2026-09-03):** The CMake target graph remains modular, but the generated solution is no longer allowed to expose that entire graph as a flat list. `USE_FOLDERS` and `PREDEFINED_TARGETS_FOLDER` are enabled centrally in `cmake/SolutionLayout.cmake`. The primary `SwimEngine` executable stays at the solution root; the reusable first-party `SwimPlatform` and `SwimInput` targets live under `Engine Modules`; validation executables/object targets live under `Tests`; smoke/demo executables live under `Examples`; generated CMake projects live under `CMake`; and dependency projects live under `Third Party`, with the large SDL3, Draco, WebP, GLAD, PhysX, zstd, and Basis graphs grouped by dependency where applicable. This is presentation-only and does not duplicate source ownership: Platform/Input sources remain excluded from the `SwimEngine` source glob and are linked exactly once through their module libraries. Tests/examples are now `EXCLUDE_FROM_ALL`, so they remain explicitly buildable from Visual Studio without participating in the normal engine build. Both Windows clean and soft scripts compile the primary Ninja tree first, then regenerate the Visual Studio solution and validate that the required solution folders exist; the standalone solution-generation script uses the same toolchain discovery and validation path.

**Ninja manifest stability checkpoint (2026-09-03):** Windows soft/clean workflows keep the primary Ninja configure and build contiguous and refresh the secondary Visual Studio solution only after a successful primary build. First-party and shader source discovery remains configure-time globbing but no longer uses `CONFIGURE_DEPENDS`. A real clean Windows run proved that removing first-party glob watching was not sufficient because a fetched third-party project can still add its own `VerifyGlobs` edge; the resulting Ninja manifest repeatedly ran CMake (`[0/2]`, `[0/4]`, `[0/6]`, ...) without ever reaching compilation. Swim's Ninja presets now set `CMAKE_SUPPRESS_REGENERATION=ON`, and the top-level project forces the same contract for every Ninja generator. This is safe because every supported clean/soft workflow explicitly configures immediately before building, so source/dependency graph changes are still discovered before compilation while dependency-owned automatic regeneration cannot trap Ninja in a manifest loop. Both Windows scripts call `Assert-SwimNinjaManifestStable` after configure and refuse to invoke Ninja if `RERUN_CMAKE`, `VerifyGlobs.cmake`, or `cmake.verify_globs` appears in `build.ninja`. Configure-time generated Basis transcoder source remains write-if-different so unchanged configuration also preserves its timestamp. `verify-build-layout.py` guards these invariants.


**Current foundation/build status (2026-09-03):** the dependency/bootstrap problems found by real Windows clean builds are now considered resolved enough for normal iteration: clean deletion is idempotent and long-path aware, the CPM cache is integrity-checked, nlohmann/json uses a pinned verified single-header artifact, PhysX builds from the short detached `build/.px` worktree, and the solution is regenerated by both Windows workflows. The real MSVC build has progressed from Platform errors through PhysX, PCH, renderer Win32/Vulkan include issues, and into the final first-party objects; the latest known compile checkpoint reached roughly 668/676 before a stale wide-string editor command in `Scene.cpp`, which has now been corrected. **Use the soft Windows build for normal C++ iteration from here.** Return to a clean build only when dependency declarations/pins, cache layout, or generated dependency-build contracts change, or when an integrity check explicitly requires it.

```text
Build-time availability
    Vulkan backend: enabled
    OpenGL legacy backend: enabled
    PhysX backend: enabled
    Jolt backend: enabled

Runtime selection
    GraphicsBackend::Vulkan
    PhysicsBackend::Jolt
```

A useful target dependency shape is shown below. `A -> B` means **A depends on B**:

```text
Swim::Engine      -> Render, Scene, Physics, Animation, Audio, Ui, Input, Assets, Jobs, Io, Platform, Core
Swim::Render      -> Rhi, Assets, Jobs, Core
Swim::RhiVulkan   -> Rhi, Platform, Core, Vulkan-Headers, volk, vk-bootstrap, VMA
Swim::Rhi         -> Core
Swim::PhysicsPhysX-> Physics, Jobs, Core, PhysX
Swim::PhysicsJolt -> Physics, Jobs, Core, Jolt
Swim::Physics     -> Core
Swim::Scene       -> Assets, Jobs, Core, EnTT
Swim::Assets      -> Io, Jobs, Platform, Core
Swim::Input       -> Platform, Core
Swim::Io          -> Platform, Jobs, Core
Swim::Jobs        -> Core, enkiTS
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
- `Swim::Input` is a standalone first-party target over normalized platform events. The legacy `InputManager` is now an adapter rather than the owner of Win32 message/key semantics.
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
- Continue the real Windows build from the current ~668/676 checkpoint through the remaining objects, final link, and launch. The stale editor-command compile path has been fixed; any next failures should now be treated as first-party C++/link/runtime issues unless the dependency-integrity checks say otherwise.
- Explicitly build the `SwimPlatformPublicHeaders` validation target on Windows so the Windows half of the generic-public-header exit criterion is proven rather than inferred from the main engine compile.
- The exit criterion requiring headless Core/Jobs/Assets initialization remains intentionally open because Jobs and the authoritative Assets architecture are later phases and do not exist yet.

**Clean-cache hardening:** real Windows clean runs exposed two independent deletion hazards: preserving `build/.px` while removing `.cache` could leave a short PhysX alias/worktree tied to a deleted checkout, and Windows PowerShell recursive deletion could fail halfway through old dependency caches containing paths beyond `MAX_PATH`, leaving a partially removed tree that then failed differently on the next run. Clean builds now remove all repository-local cache state and the short PhysX path before fetching. Windows cleanup is explicitly idempotent: already-absent paths and broken legacy junctions count as success, directory-entry existence is checked without requiring a junction target to resolve, and recursive deletion uses Windows extended-length (`\\?\`) paths through native `rd /s /q` rather than `Remove-Item -Recurse`. The post-delete verification uses the same directory-entry test, so a broken reparse point cannot be mistaken for a successful clean. PhysX builds from a detached Git worktree at `build/.px`, keeping generated NVIDIA projects/binaries isolated from the pinned CPM checkout. Configure-time dependency integrity checks fail immediately on any dirty Git-backed cache instead of allowing dirty-source state to break much later in PCH/compiler work.

**Windows renderer compile checkpoint:** after clean-state deletion, immutable dependency-cache setup, nlohmann single-header acquisition, and the short PhysX worktree all succeeded in the real MSVC build, the legacy engine advanced into renderer compilation (roughly 632/676). The large error burst was two Win32 include-boundary regressions exposed by removing `Windows.h` from the generic PCH: legacy `min`/`max` macros were leaking from renderer-local Windows headers and corrupting `std::min`, `std::max`, and `std::numeric_limits<T>::max()` expressions across OpenGL/Vulkan code; separately, the Vulkan Win32 surface declarations were missing because `VK_USE_PLATFORM_WIN32_KHR` was not guaranteed before `<vulkan/vulkan.h>`. The renderer now consumes a centralized internal `WindowsApi.h` wrapper that defines `WIN32_LEAN_AND_MEAN` and `NOMINMAX`, the legacy Windows target also carries those definitions defensively, and the Vulkan backend defines/enforces `VK_USE_PLATFORM_WIN32_KHR` before Vulkan headers. Build-layout verification now rejects raw renderer `Windows.h` includes and checks the Vulkan platform-define ordering.

**Windows scene/editor compile checkpoint:** the next real MSVC run confirmed the Win32 macro and Vulkan surface fixes and advanced to roughly 668/676 with only `Scene.cpp` failing. The remaining error was a stale pre-refactor editor-hotkey helper still forwarding `const wchar_t*` commands into the now platform-neutral UTF-8 `SwimEngine::OnEditorCommand(std::string_view)` API. Scene hotkeys now use narrow UTF-8 command literals through `std::string_view`, and verification rejects reintroducing the old wide-string command path. At this point the dependency cache and PhysX worktree have been established successfully by a true clean build; normal first-party C++ iteration should use the soft build unless dependency declarations, pins, or generated dependency-cache layout change.

### Phase 1 exit criteria

- [ ] A `HelloWindow` application runs on Windows and Linux from the same public API. *(Implementation exists; real SDL-backed runtime verification is still pending on both OSes.)*
- [x] window resize/minimize/focus/DPI events are normalized.
- [x] keyboard/mouse/controller APIs contain no Win32 key/message types.
- [ ] a headless application can initialize Core/Jobs/Assets tests with no window. *(Platform headless mode exists; Core/Jobs/Assets integration waits for the later subsystem phases.)*
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
- Removed core-system construction and frame iteration from `SystemManager`. `SwimEngine` now owns typed Input, Command, Scene, Physics, Renderer, and Camera slots and spells out Awake, Init, Update, FixedUpdate, and reverse-order Exit directly. The old dynamic `SystemManager` type remains available only as an unreferenced legacy/dynamic registry rather than the engine's core ownership model.
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
- A full legacy Windows compile is still required for the changed `SwimEngine` and renderer-facing compatibility code; use the established soft Windows build for that first-party iteration rather than resetting the dependency cache.

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

This checkpoint deliberately stops before the Async IO and transient-memory portions below. They are the next Phase 3 work and should be added on top of a Windows/MSVC-validated scheduler boundary rather than mixed into the same broad ownership migration.

### Async IO service

Create a platform-neutral IO layer:

- [ ] async full-file read;
- [ ] async range read;
- [ ] memory mapping;
- [ ] priorities;
- [ ] cancellation;
- [ ] batch/adjacent-read opportunities;
- [ ] completion on a known executor/thread;
- [ ] explicit blocking API for tools/bootstrap/tests only.

Do not hide blocking filesystem work behind an apparently cheap asset getter.

### Memory strategy

Add simple, purposeful allocators before hot paths proliferate:

- [ ] per-frame CPU arena;
- [ ] per-job scratch arena or thread scratch;
- [ ] temporary import/compiler arenas where useful;
- [ ] persistent registries use stable containers/slot maps rather than frame allocation.

Do not build a giant custom general allocator unless profiling demands it.

### Phase 3 exit criteria

- [x] renderer code can use a general `ParallelFor` without knowing enkiTS.
- [ ] asset loader has a non-blocking read primitive available.
- [x] no new thread-per-file or thread-per-subsystem patterns; compute and reserved blocking work share the engine-owned scheduler.
- [ ] jobs and IO shut down deterministically.

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

- [ ] stable identity independent of a raw pointer;
- [ ] typed handles;
- [ ] stale-handle detection/generation where useful;
- [ ] path-to-AssetId database for authoring/dev;
- [ ] content hash for incremental compilation and deduplication;
- [ ] dependency graph;
- [ ] explicit load state;
- [ ] explicit errors;
- [ ] handles can exist before residency completes.

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
- skin/morph stream references where applicable.

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

- [ ] nodes/hierarchy;
- [ ] transforms;
- [ ] meshes/primitives;
- [ ] material slots;
- [ ] metallic-roughness materials;
- [ ] textures/samplers;
- [ ] skins/skeletons;
- [ ] animation channels;
- [ ] morph targets;
- [ ] relevant cameras/lights if desired;
- [ ] deliberately supported extensions.

Then run offline processing:

- [ ] generate missing tangents;
- [ ] optimize vertex cache;
- [ ] optimize vertex fetch;
- [ ] optimize overdraw where appropriate;
- [ ] generate LODs if configured;
- [ ] generate meshlets;
- [ ] pack/quantize runtime vertex formats;
- [ ] convert textures to KTX2/native compressed formats;
- [ ] decode Draco only in import/compiler path when source uses it.

### KTX2 runtime texture path

Use KTX2 as the normal compiled texture container.

Support:

- [ ] mip chains;
- [ ] sRGB/linear metadata;
- [ ] normal-map policy;
- [ ] BC family on desktop where appropriate;
- [ ] ASTC/ETC variants for future mobile;
- [ ] BasisU transcoding when the distribution strategy benefits from it;
- [ ] cubemaps/arrays;
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

- [ ] a glTF/GLB source model compiles through fastgltf into Swim runtime assets.
- [ ] runtime model loading does not require tinygltf/fastgltf object graphs.
- [ ] mesh/material/texture are independent asset identities.
- [ ] no asset constructor uploads to a renderer.
- [ ] no asset registry is a process-global singleton.
- [ ] content hashing replaces O(N) raw-byte pool scans for deduplication.

---

## Phase 5 — Scene/ECS cleanup and render-facing data boundaries

Do this before GPU Scene implementation.

### Keep EnTT

There is no architectural reason to replace EnTT.

The work is around ownership and component boundaries.

### Scene ownership

- [ ] `SceneManager` owns scenes explicitly.
- [ ] scenes are not globally preregistered through a static factory.
- [ ] an active scene is an application concept, not a dependency used by low-level components.
- [ ] scene identity is explicit.
- [ ] multiple loaded scenes are supported.
- [ ] headless scenes work.

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
- [ ] `TransformSystem` is scene-owned.
- [ ] dirty queue/versioning is scene-owned.
- [ ] hierarchy propagation is explicit and batchable.
- [ ] no transform method discovers the active scene globally.
- [ ] no graphics API branch exists in Transform.
- [ ] physics interpolation state is either a separate component/system or a clearly generic transform interpolation facility.

### Canonical coordinate and clip-space convention

Choose the convention once so Camera and Transform remain backend-neutral.

Recommended modern convention:

- right-handed world space;
- depth range 0..1;
- a consistent screen/UI origin defined by UI, not by graphics API;
- renderer controls viewport orientation/front-face handling;
- adopt reverse-Z when the modern depth pipeline is established, before HZB/occlusion code depends on depth semantics.

Vulkan, D3D12, and Metal should adapt at the RHI/backend boundary rather than modifying Camera math. Legacy OpenGL 4.6 can use clip-control/backend adjustments where necessary.

### Camera/view redesign

A single global camera/frustum is insufficient.

Create:

- [ ] `CameraComponent` or reusable Camera data;
- [ ] `RenderView`/`ViewDesc`;
- [ ] per-view frustum;
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

- [ ] `BehaviorRegistry` is owned by runtime/tool context rather than a mutable process-global singleton.
- [ ] behavior factory registration remains data-driven.
- [ ] behaviors receive explicit scene/service context.
- [ ] behaviors do not cache shared ownership of core services by default.
- [ ] behavior execution is a gameplay/scene phase, never renderer traversal.

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

- [ ] no mutable static vector of live scenes;
- [ ] no scene constructor depends on a global Engine;
- [ ] scene type registration is deterministic and testable;
- [ ] a `SceneId`/`SceneHandle` identifies loaded scene instances;
- [ ] multiple loaded scenes are legal even if an application designates one as the primary gameplay scene;
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

- [ ] serializer code does not call `WM_COPYDATA`, open files, or locate executable directories;
- [ ] storage does not know about editor commands;
- [ ] editor transport does not own scene serialization policy;
- [ ] parent/child references serialize stable entity IDs, not integral `entt::entity` values;
- [ ] asset references serialize `AssetId` plus optional debug/source provenance rather than relying on fuzzy pool names;
- [ ] runtime-only entities/components can opt out explicitly;
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

- [ ] Transform dirty state is per scene.
- [ ] Frustum/view state is per view.
- [ ] Scene has no Vulkan/OpenGL renderer pointer.
- [ ] components do not discover the active scene through a global engine.
- [ ] renderable components contain asset/render handles, not GPU objects.
- [ ] multiple scenes can exist without shared transform/frustum globals.
- [ ] scene types register without constructing live scenes during static initialization.
- [ ] persisted entity references use stable serialization IDs rather than raw EnTT values.
- [ ] scene asset references use `AssetId`.
- [ ] scene serialization, storage, and editor transport are independent modules.

---

## Phase 6 — Physics abstraction with PhysX and Jolt parity baseline

The physics seam should be proven before more gameplay becomes dependent on PhysX details.

### Public physics concepts

Create Swim-owned types:

- [ ] `PhysicsSystem`
- [ ] `PhysicsWorld`
- [ ] `BodyHandle`
- [ ] `ShapeHandle`
- [ ] `PhysicsMaterialHandle`
- [ ] `ConstraintHandle`
- [ ] `CharacterHandle`
- [ ] `BodyDesc`
- [ ] `ShapeDesc`
- [ ] `PhysicsMaterialDesc`
- [ ] `CollisionLayer`
- [ ] `RaycastHit`
- [ ] `SweepHit`
- [ ] `OverlapHit`
- [ ] `CollisionEvent`
- [ ] `TriggerEvent`

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

- [ ] static bodies;
- [ ] dynamic bodies;
- [ ] kinematic bodies;
- [ ] box/sphere/capsule;
- [ ] convex mesh;
- [ ] triangle mesh;
- [ ] gravity;
- [ ] mass/damping;
- [ ] forces/impulses;
- [ ] velocity;
- [ ] collision filtering;
- [ ] triggers;
- [ ] raycast;
- [ ] sweep;
- [ ] overlap;
- [ ] collision events;
- [ ] fixed-step simulation;
- [ ] transform interpolation;
- [ ] safe destruction during/around simulation.

### Cooking

Collision cooking is an asset/compiler responsibility where possible.

- [ ] model import can produce collision data;
- [ ] convex/triangle cooked payloads can have backend-specific compiled variants;
- [ ] runtime does not synchronously cook expensive collision geometry on the main thread by default.

### Jobs

Where practical, physics backend worker integration should use or cooperate with the engine jobs system. Do not force identical internal scheduling if a backend has a better native strategy; keep the integration seam explicit.

### Phase 6 exit criteria

- [ ] same `PhysicsSandbox` runs with `--physics=physx` and `--physics=jolt`.
- [ ] gameplay/scene generic headers contain no PhysX/Jolt implementation types.
- [ ] backend switching requires no gameplay recompilation logic or `if constexpr` branching.

---

## Phase 7 — Slang shader system and reflection contracts

Do this before final RHI descriptor/pipeline architecture is locked down.

### Source policy

- [ ] all new first-party shaders use `.slang`;
- [ ] existing HLSL can migrate incrementally because Slang is HLSL-oriented;
- [ ] Vulkan shipping output is SPIR-V;
- [ ] OpenGL legacy can consume generated GLSL/SPIR-V where the path is reliable;
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

### Phase 7 exit criteria

- [ ] one Slang shader compiles to Vulkan SPIR-V and reflects its bindings.
- [ ] the same source can produce a legacy OpenGL-consumable artifact for a validation sample where practical.
- [ ] C++/shader parameter layout validation exists.
- [ ] descriptor/pipeline layout design no longer duplicates binding definitions manually in multiple source files.

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

- [ ] `GraphicsSystem`
- [ ] `Adapter`
- [ ] `Device`
- [ ] `Queue`
- [ ] `Swapchain`
- [ ] `CommandPool` / `CommandAllocator`
- [ ] `CommandList`
- [ ] `Buffer`
- [ ] `Texture`
- [ ] `TextureView`
- [ ] `Sampler`
- [ ] `ShaderProgram`
- [ ] `PipelineLayout`
- [ ] `GraphicsPipeline`
- [ ] `ComputePipeline`
- [ ] `DescriptorSchema` / resource layout
- [ ] `DescriptorTable` / bind group/resource table
- [ ] `Fence`
- [ ] `Timeline`
- [ ] `QueryPool`

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

- [ ] no Vulkan type exists in generic render/RHI public headers.
- [ ] RHI accepts the platform window abstraction, not `HWND`.
- [ ] graphics backend is selected at runtime.
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

- [ ] dynamic rendering;
- [ ] synchronization2;
- [ ] timeline semaphores;
- [ ] descriptor indexing;
- [ ] indirect count;
- [ ] buffer device address where beneficial;
- [ ] pipeline cache;
- [ ] debug utils;
- [ ] memory-budget telemetry.

Optional/capability-gated:

- descriptor buffer;
- mesh/task shader path;
- ray query/ray tracing;
- variable-rate shading;
- sparse resources.

### Resource allocation

Replace raw general `vkAllocateMemory` usage with VMA.

Create:

- [ ] device-local allocation policy;
- [ ] persistent mapped upload arenas/rings;
- [ ] readback arenas;
- [ ] transient frame buffers via render graph;
- [ ] memory-budget reporting;
- [ ] allocation debug names/tags.

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

- [ ] resize/recreate without normal device-idle;
- [ ] minimized/zero-size window handling;
- [ ] SDR baseline;
- [ ] HDR capability path;
- [ ] present mode configuration;
- [ ] multiple swapchains/windows supported at RHI level.

### Validation and diagnostics

- [ ] validation enabled in debug/CI mode;
- [ ] Vulkan object names;
- [ ] command labels;
- [ ] adapter/driver info logging;
- [ ] device-loss diagnostics;
- [ ] RenderDoc-friendly markers;
- [ ] GPU timestamps.

### Phase 9 exit criteria

- [ ] clear/triangle/texture test on Windows and Linux.
- [ ] validation clean.
- [ ] VMA used for normal buffer/image allocation.
- [ ] resize/minimize/restore loop is stable.
- [ ] frame lifetime uses timeline-based retirement.

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

- [ ] pass dependency DAG;
- [ ] read/write declarations;
- [ ] read-before-write validation;
- [ ] cycle detection;
- [ ] resource lifetime analysis;
- [ ] image layout/resource-state transitions;
- [ ] memory barriers;
- [ ] queue ownership transfer;
- [ ] pass culling;
- [ ] imported persistent resources;
- [ ] exported resources;
- [ ] transient resource pooling;
- [ ] graphics/compute/transfer passes;
- [ ] GPU labels/timestamps per pass;
- [ ] deterministic ordering where dependencies are otherwise equal.

### Async compute

Do not force async-compute complexity immediately.

First:

1. make dependencies correct;
2. profile pass overlap opportunities;
3. schedule async only when it produces measurable value.

### Phase 10 exit criteria

- [ ] an offscreen pass -> post pass -> present sequence uses graph-generated synchronization.
- [ ] hand-written barriers are no longer scattered through high-level renderer features.
- [ ] graph debug dump/view exists.

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

Do not use shared owning pointers as IDs.

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

- [ ] large device-local pages rather than one fragile fixed buffer;
- [ ] variable range allocation;
- [ ] stable metadata indirection;
- [ ] 16/32-bit indices;
- [ ] multiple packed vertex formats;
- [ ] LOD ranges;
- [ ] meshlet ranges;
- [ ] async transfer upload;
- [ ] safe deferred free;
- [ ] fragmentation metrics;
- [ ] optional relocation/compaction later.

### Texture residency

- [ ] bindless texture table;
- [ ] separate image identity from sampler identity;
- [ ] fallback texture;
- [ ] timeline-retired bindless IDs;
- [ ] residency state separate from AssetHandle validity;
- [ ] KTX2/native compressed upload;
- [ ] future mip streaming metadata.

### No hidden upload

Asset load and GPU residency are explicit asynchronous state transitions:

```text
Unloaded
 -> Queued
 -> Reading
 -> Decoding/Decompressing if needed
 -> WaitingForGpuUpload
 -> Uploading
 -> Resident
```

### Phase 11 exit criteria

- [ ] one compiled model uploads without source importer involvement.
- [ ] large geometry uses paged GeometryHeap allocation.
- [ ] texture becomes bindless through an explicit residency operation.
- [ ] GPU resource destruction is timeline safe.

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

- [ ] upload only changed transforms/materials/lights;
- [ ] batch adjacent ranges;
- [ ] retain previous transform for motion vectors;
- [ ] stable object IDs;
- [ ] scene unload destroys object handles through deferred GPU-safe retirement.

### Phase 12 exit criteria

- [ ] 100k persistent objects render with dirty-only CPU->GPU updates.
- [ ] renderer can create a RenderObject without EnTT.
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

- [ ] no CPU-visible-list round trip for frame correctness;
- [ ] HZB built each appropriate frame;
- [ ] conservative behavior for newly visible/teleported objects;
- [ ] camera cut invalidates occlusion history;
- [ ] GPU compaction;
- [ ] GPU draw counts;
- [ ] LOD hysteresis;
- [ ] bounded binning structures;
- [ ] asynchronous diagnostic counters only;
- [ ] indirect-count fast path;
- [ ] fallback path only for capabilities that genuinely require it.

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

### Phase 13 exit criteria

- [ ] 100k mixed objects render without CPU visibility feedback.
- [ ] occlusion-heavy benchmark shows HZB benefit.
- [ ] camera cuts/teleports do not incorrectly occlude objects.
- [ ] GPU visibility stats are readable asynchronously for debugging.

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

- [ ] immutable shared templates;
- [ ] cheap mutable instances;
- [ ] reflected typed parameters;
- [ ] GPU material parameter buffer;
- [ ] bindless textures/samplers;
- [ ] alpha opaque/mask/blend/additive;
- [ ] double-sided/cull policy;
- [ ] custom game material templates;
- [ ] hot reload;
- [ ] deterministic fallback material;
- [ ] no renderer-wide `switch` for every material feature;
- [ ] no mesh ownership in material.

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

- [ ] PBR gallery matches reference expectations.
- [ ] materials are independent from mesh assets.
- [ ] shader reflection drives layout validation.
- [ ] material changes update only affected GPU ranges.

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

- [ ] configurable X/Y screen tiles;
- [ ] configurable Z slices;
- [ ] logarithmic/depth-aware Z partitioning;
- [ ] view-relative bounds;
- [ ] resolution changes regenerate grid parameters cleanly.

### Light assignment

- [ ] point lights;
- [ ] spot lights;
- [ ] directional lights handled outside local cluster lists;
- [ ] GPU cluster/light intersection;
- [ ] compact index storage;
- [ ] bounded overflow behavior;
- [ ] overflow counters;
- [ ] debug heatmap;
- [ ] no per-object CPU light list.

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

- [ ] thousands of dynamic lights scale predictably.
- [ ] opaque and transparent rendering consume the clustered data path.
- [ ] overflow behavior is visible and safe.
- [ ] zero/few-light scenes remain cheap.

---

## Phase 16 — Shadows

### Directional

- [ ] cascaded shadow maps or another stable first implementation;
- [ ] stable cascade snapping;
- [ ] GPU-driven caster culling;
- [ ] alpha-mask shadow variant;
- [ ] configurable cascade count/resolution.

### Spot

- [ ] shadow atlas allocator;
- [ ] stable allocation where possible;
- [ ] budget/eviction policy;
- [ ] GPU caster culling.

### Point

- [ ] cube/array strategy only for selected shadow-casting lights;
- [ ] explicit cost controls.

### Filtering/bias

- PCF baseline;
- slope/depth/normal-offset policy;
- later EVSM/VSM/PCSS experiments as modules.

### Integration

`GpuLight` stores a shadow handle/index. Clustered lighting does not need to know shadow implementation internals.

---

## Phase 17 — Environment, HDR, and post-processing

### Environment

Rebuild cubemap/environment rendering as generic render passes/assets rather than backend-owned cube-map classes.

- [ ] sky environment;
- [ ] IBL irradiance;
- [ ] prefiltered specular environment;
- [ ] environment rotation/intensity;
- [ ] HDR environment asset support.

### Post stack

Recommended order:

1. HDR scene color;
2. exposure;
3. tone mapping;
4. bloom;
5. color grading;
6. TAA once motion vectors/history are solid;
7. optional GTAO/SSAO;
8. optional SSR;
9. fog;
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

### Runtime

- [ ] `SkeletonAsset`;
- [ ] `SkeletonInstance`;
- [ ] `AnimationClip`;
- [ ] `Animator`;
- [ ] layers;
- [ ] state machine;
- [ ] crossfade;
- [ ] additive animation;
- [ ] bone masks;
- [ ] root motion;
- [ ] events;
- [ ] playback speed/direction/looping;
- [ ] sockets/attachments;
- [ ] morph weights.

### Jobs/GPU

- jobify clip sampling/blending;
- GPU skinning baseline;
- choose vertex vs compute skinning by measured workload;
- preserve previous state for motion vectors;
- explicit bridge to physics ragdolls later.

---

## Phase 19 — GPU particles

- [ ] emitter assets/components;
- [ ] GPU particle pool;
- [ ] GPU spawn command buffer;
- [ ] compute simulation;
- [ ] death/compaction;
- [ ] billboard/mesh/trail rendering;
- [ ] local/world space;
- [ ] curves;
- [ ] flipbooks;
- [ ] optional collisions;
- [ ] indirect generation;
- [ ] transparency/sorting policy.

Particles are a GPU Scene/render-graph producer, not thousands of normal EnTT mesh entities.

---

## Phase 20 — Runtime UI and text

### Separate UI from world Transform hacks

Create a dedicated retained UI tree.

Suggested concepts:

- `UiDocument`
- `UiNode`
- `UiStyle`
- `UiLayout`
- `UiImage`
- `UiText`
- `UiButton`
- `UiScrollView`
- `UiInputField`
- `UiCanvas`
- `UiClip`
- `UiFocusManager`
- `UiEventRouter`

### Layout

- measure -> layout -> paint;
- parent/child hierarchy;
- anchors;
- margin/padding;
- min/max/preferred size;
- stack/flex-like layout;
- absolute placement;
- aspect ratio;
- percentage sizing;
- DPI-aware logical units;
- scrolling/clipping;
- dirty-only invalidation.

### Text

Replace handwritten Unicode parsing/kerning-only layout with:

- FreeType;
- HarfBuzz;
- Unicode shaping;
- font fallback;
- bidirectional/RTL support strategy;
- line breaking/wrapping;
- selection/caret;
- IME visualization;
- MSDF atlas pages.

### Rendering

- batched instanced quads;
- bindless images/glyph atlases;
- rounded/SDF primitives;
- borders/nine-slice;
- clipping/scissor indexing;
- premultiplied alpha policy;
- HDR-aware composition.

World-space text may reuse shaping/atlas services while producing world render instances.

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

Run the same behavioral suite against PhysX and Jolt.

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
- imported/exported resources.

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

---

# Part V — Repository organization target

## 33. Suggested source layout

The exact folder names can vary, but dependency direction should be visible in the tree.

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
```

Build targets should mirror these module boundaries so dependency direction is visible and enforceable.

---

## 34. Current-code integration map

| Current code | Target treatment |
| --- | --- |
| `SwimEngine.*` | Split window/platform work, runtime composition, config parsing, game loop, and optional editor bridge. Remove global service-locator role. |
| `Machine.h` | Keep only as an optional lifecycle convenience; core services do not all need to derive from it. |
| `SystemManager.*` | Replace core string-based service ownership with explicit typed composition; retain a dynamic registry only if plugins need it. |
| `PCH.*` | Remove OS/RHI/backend leakage. |
| `InputManager.*` | Replace with SDL3-backed generic input/event/action-map system. |
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
| `Source/Shaders/Vulkan/*` | Migrate first-party shader source to Slang modules. |
| `Source/Shaders/OpenGL/*` | Keep as legacy during migration; replace with Slang-generated/ported shaders where practical. |
| `Source/Game/*` | Update incrementally to use EngineServices, generic Input, AssetHandle, MaterialHandle, and generic Physics APIs. |

---

# Part VI — Exact implementation order / PR ladder

## 35. Critical path

This is the recommended order for actual implementation. Do not skip ahead to a major renderer feature if an earlier contract it depends on is still temporary. Every PR should preserve the CMake target/dependency rules in Section 4.2 as it introduces new platform code, libraries, generated artifacts, or backend implementations.

### 35.1 Foundation

1. [ ] Create clean `Core` public include boundary and remove backend/platform dependencies from generic PCH.
2. [ ] Add SDL3-backed `PlatformSystem` and `Window` abstraction.
3. [ ] Move Win32 window creation/message handling out of `SwimEngine`.
4. [ ] Implement Windows external/native-window wrapping needed by editor hosting.
5. [ ] Add Linux window path and `HelloWindow` test.
6. [ ] Replace `InputManager` with SDL3 normalized InputSystem + action API.
7. [ ] Add filesystem roots/path/mapped-file/dynamic-library platform APIs.
8. [ ] Replace global `SwimEngine` dependency discovery with explicit Engine composition/services.
9. [ ] Add runtime `GraphicsBackend` and `PhysicsBackend` config/launcher parsing.
10. [ ] Introduce enkiTS-backed Jobs service and retire renderer-global worker ownership.
11. [ ] Add async/range IO service.

### 35.2 Data and scene foundations

12. [ ] Introduce `AssetId`, typed `AssetHandle<T>`, asset registry, load state, dependency graph.
13. [ ] Split Mesh/Texture/Material CPU identity from GPU/backend resources.
14. [ ] Remove mesh ownership from material data.
15. [ ] Build fastgltf-based source importer + intermediate model representation.
16. [ ] Add meshoptimizer offline processing.
17. [ ] Add KTX2 compiler/runtime metadata path.
18. [ ] Define `.sasset` v1 and compile/load one static model.
19. [ ] Replace global asset pools with engine-owned asset services.
20. [ ] Replace static transform dirty state with scene-owned TransformSystem.
21. [ ] Replace static global Frustum with per-view state.
22. [ ] Replace global EntityFactory queue with scene command buffer.
23. [ ] Replace static live-scene preregistration with explicit SceneCatalog factories and loaded `SceneHandle` identity.
24. [ ] Split scene serialization/storage/tooling transport; add durable entity IDs and `AssetId` scene references.
25. [ ] Remove renderer backend pointers from Scene/Behavior APIs.
26. [ ] Establish canonical coordinate/clip-space convention.
27. [ ] Build generic physics handles/contracts.
28. [ ] Move current PhysX implementation behind generic backend.
29. [ ] Add Jolt backend baseline and shared parity tests.

### 35.3 Shader/RHI foundation

30. [ ] Integrate Slang compiler and reflection metadata.
31. [ ] Port a minimal shader set to Slang.
32. [ ] Define RHI formats/resource states/descriptors/capability table.
33. [ ] Define RHI Device/Queue/Swapchain/Buffer/Texture/Sampler/Pipeline contracts.
34. [ ] Add runtime graphics factory.
35. [ ] Build Vulkan RHI instance/adapter/device using volk + vk-bootstrap.
36. [ ] Create Vulkan surface from Platform window through SDL3 WSI.
37. [ ] Add VMA buffer/image allocation.
38. [ ] Add timeline/frame-context/deferred-destruction model.
39. [ ] Validation-clean RHI clear/triangle/texture on Windows and Linux.

### 35.4 Modern renderer foundation

40. [ ] Implement RenderGraph DAG/resource-state/barrier system.
41. [ ] Implement upload/readback arenas and transfer helpers.
42. [ ] Implement generational GPU resource registries.
43. [ ] Implement paged GeometryHeap.
44. [ ] Connect compiled MeshAsset/TextureAsset to asynchronous GPU residency.
45. [ ] Implement bindless texture/sampler table with timeline-safe ID reuse.
46. [ ] Implement persistent GPU Scene records.
47. [ ] Implement EnTT render extraction -> RenderObject updates.
48. [ ] Reach 100k object GPU Scene stress with dirty-only updates.

### 35.5 GPU-driven renderer

49. [ ] GPU frustum culling.
50. [ ] depth/HZB build using final reverse-Z/depth convention.
51. [ ] occlusion culling with history invalidation.
52. [ ] LOD selection/hysteresis.
53. [ ] visible compaction.
54. [ ] material/pass binning.
55. [ ] indirect command/count generation.
56. [ ] remove CPU-visible-list dependency from normal world rendering.
57. [ ] add visibility diagnostics/benchmarks.

### 35.6 Shading and lighting

58. [ ] MaterialTemplate/MaterialInstance + reflected parameters.
59. [ ] GPU material buffer/bindless material resources.
60. [ ] metallic-roughness PBR.
61. [ ] environment/IBL.
62. [ ] PBR image regression gallery.
63. [ ] GpuLightBuffer.
64. [ ] clustered grid.
65. [ ] GPU light assignment/compaction.
66. [ ] opaque Clustered Forward+.
67. [ ] transparent Clustered Forward+.
68. [ ] cluster heatmap/overflow diagnostics.
69. [ ] 1k/10k+ light benchmarks.

### 35.7 Complete modern frame

70. [ ] directional shadows.
71. [ ] spot atlas.
72. [ ] point shadow policy.
73. [ ] HDR scene color/exposure/tone mapping.
74. [ ] bloom/color grading.
75. [ ] motion vectors/TAA.
76. [ ] optional AO/SSR/fog modules.
77. [ ] GPU particles.
78. [ ] animation/skinning/morphs.
79. [ ] runtime UI + HarfBuzz/FreeType/MSDF.
80. [ ] miniaudio audio system.
81. [ ] `.spack` streaming/residency budgets/eviction.

### 35.8 Compatibility and hardening

82. [ ] Move OpenGL under legacy module and make it consume Platform/Input abstractions.
83. [ ] Remove Vulkan/OpenGL behavior from generic Vertex/Texture/Transform/Camera types.
84. [ ] Move editor IPC into optional tooling transport adapter.
85. [ ] Move gizmos/editor camera to tooling/debug module.
86. [ ] Windows/Linux long-run/perf/validation hardening.
87. [ ] external engine consumer/API examples.
88. [ ] prepare Apple/Android platform/RHI/backend seams without implementing speculative platform code early.

---

## 36. Gates that prevent doing work in the wrong order

Before starting **RHI/Vulkan modernization**:

- [ ] generic Window abstraction exists;
- [ ] no RHI API takes `HWND`;
- [ ] runtime graphics backend selection exists;
- [ ] generic PCH no longer imports Vulkan/Win32 globally.

Before starting **GPU Scene**:

- [ ] live scene construction is explicit rather than static preregistration;
- [ ] persistent entity/asset identity is separate from runtime EnTT handles/source paths;
- [ ] AssetHandle exists;
- [ ] mesh/material are independent;
- [ ] scene Transform dirty state is not process-global;
- [ ] render extraction boundary exists.

Before starting **HZB/occlusion**:

- [ ] canonical depth convention is final;
- [ ] reverse-Z decision is final;
- [ ] per-view history/camera-cut state exists.

Before starting **Clustered Forward+**:

- [ ] GPU Scene is persistent;
- [ ] GpuLight schema exists;
- [ ] Slang reflection/material binding is stable;
- [ ] PBR baseline works;
- [ ] RenderGraph is authoritative.

Before expanding **physics gameplay**:

- [ ] Rigidbody contains no PhysX/Jolt pointer;
- [ ] PhysX/Jolt parity baseline runs through the same generic API.

Before expanding **asset streaming**:

- [ ] runtime consumes `.sasset/.spack` identities, not source filenames;
- [ ] async IO exists;
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
- [ ] source import is tool-side and fastgltf-based.
- [ ] compiled runtime assets are versioned, upload-friendly, and streamable.
- [ ] mesh/material/texture identity is clean and non-singleton.
- [ ] GPU Scene is persistent and independent from EnTT.
- [ ] visibility and draw generation are GPU-driven with no CPU feedback requirement.
- [ ] GeometryHeap and bindless resources use safe deferred lifetimes.
- [ ] metallic-roughness PBR + IBL is production baseline.
- [ ] Clustered Forward+ is the standard local-light path.
- [ ] shadows/HDR/post are RenderGraph modules.
- [ ] PhysX and Jolt both implement the generic physics API and are runtime selectable.
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
