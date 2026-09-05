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

## Current implementation snapshot — 2026-09-05

This section is the short authoritative status summary for the current repository. Detailed historical checkpoints remain below because they explain why particular contracts exist, but this snapshot should be read first when deciding what to work on next.

- **File organization pass — Vulkan RHI and physics backends:** the monolithic `VulkanRhiBackend.cpp`, `JoltWorldBackend.cpp`, and `PhysXWorldBackend.cpp` — each previously a single file defining most or all of that backend's concrete types — have been split one-type-per-file under `Internal/`, `Resources/`, `Sync/`, `Commands/`, `Filters/`, and `Callbacks/` subfolders (see §0.2 for the rule and §33 for the target layout). This was pure code motion: no behavior changed. `VulkanSwapchain` was further split into a declaration-only header plus a `.cpp` for its non-trivial method bodies, matching the pattern already used by `VulkanQueue`. While validating the physics split, one genuine pre-existing latent bug was found and fixed: `JoltWorldBackend.cpp` defined `ToGlm(JPH::RVec3Arg)` unconditionally, but the header only declares it under `#ifdef JPH_DOUBLE_PRECISION`; since this project builds Jolt with `DOUBLE_PRECISION OFF` (where `RVec3Arg` aliases `Vec3Arg`), the unguarded definition collided with the `Vec3Arg` overload. The `.cpp` now matches the header's guard. No genuinely deprecated/dead code was found in either area to relocate into `Deprecated/`.
- **Engine module collapse — Visual Studio project topology:** the "Engine Modules" solution folder and every target that lived in it (`SwimCore`, `SwimMemory`, `SwimJobs`, `SwimPlatform`, `SwimInput`, `SwimCommands`, `SwimIO`, `SwimAssets`, `SwimPhysics`, `SwimPhysicsJolt`, `SwimPhysicsPhysX`, `SwimRhi`, `SwimRhiVulkan`) are retired as separate CMake targets. Their sources now compile directly into `SwimEngine` (or `SwimTests`/an example, whichever binary needs the same code — never both, to avoid duplicate-symbol link errors) and appear there as `source_group(TREE ...)` filters mirroring `Source/...` on disk, instead of as their own `.vcxproj` projects. This is a build-graph/solution-presentation change only: source-file organization within a module (§0.2) and the module dependency direction (§4.3's target graph, now a code-ownership graph) are unchanged. `Tests`, `Tools`, `Third Party`, and `Examples` solution folders are unaffected. Full rationale, the duplicate-symbol-avoidance rules, and the per-target consumer list: §4.3's "Engine module collapse checkpoint" and `docs/VisualStudioProjectStructure.md` §11.

- **Phase 1 — Platform foundation:** the SDL3-backed `Swim::Platform`/`Swim::Input` foundation, normalized window/input types, filesystem/mapped-file/dynamic-library APIs, native-window escape hatch, headless path, and generic-PCH cleanup are implemented. The old Win32 external-editor/`WM_COPYDATA` bridge is archived under top-level `Deprecated/` and has no active includes or build participation; future editor work is in-process engine UI. The remaining Phase 1 gates are runtime smoke coverage for the same `HelloWindow` API on both Windows and Linux and explicit Windows public-header validation. `SwimEngine`, Scene, Behavior, camera controls, gizmos, UI, and game behaviors now consume `Swim::Input::InputSystem` directly. The old `InputManager` wrapper is retired; input advances once after event pumping and before fixed/update consumers.
- **Retirement checkpoint — input, commands, and external editor:** `Source/Engine/Systems/IO` is gone. `Source/Engine/Input` owns normalized input; `Source/Engine/IO` owns async file IO; `Source/Engine/Commands` owns the new `Swim::Commands::CommandRegistry`. The old `InputManager`, `CommandSystem`, unused `SystemManager`, editor IPC, scene-JSON experiment, and disabled SceneSystem editor-command blocks are archived outside `Source/`. Active durable entity IDs/maps moved to `Scene/Identity`. Section **0.3** is the authoritative retirement inventory and gives the gates for moving the still-active legacy renderers/pools later.
- **Phase 2 — Engine ownership/configuration:** complete for the existing runtime. `SwimEngine::GetInstance()` is gone from first-party runtime dependency discovery, core systems have explicit typed ownership/lifecycle order, and graphics/physics backend choice is runtime configuration rather than a compile-time renderer selector.
- **Phase 3 — Jobs/IO/memory:** complete. `Swim::Jobs`, `Swim::IO`, `Swim::Memory`, mimalloc-backed frame/scratch allocation, and deterministic async/job shutdown are established before renderer/streaming expansion.
- **Phase 4 — Assets:** the engine-owned `Swim::Assets` identity/runtime schema, fastgltf importer, meshoptimizer path, KTX2/Basis metadata/transcode path, WebP/PNG/JPEG source-image compiler, compiler-side Draco decode, `.sasset` v1 writer/reader, development incremental cooker, and cooked-model compatibility residency path are implemented. Source codecs are owned by `SwimAssetCompiler`; they are not supposed to become shipping runtime model-import dependencies.
- **Phase 4 validation:** the Draco 1.5.7 embedded-consumer include-root issue remains isolated behind `Swim::AssetCompilerDraco`, and the supported Windows clean/soft builds compile/run the importer, source-image, development cook/load, and cooker validation targets automatically. The developer has confirmed the dependency-enabled Windows build path is already green, and the repository now contains the real `Assets` authoring tree supplied for this checkpoint (including the Sponza KTX/Draco variants, WebP sofa, barrel/test models, fonts, and textures). The build scripts continue to enforce that gate rather than relying on this one confirmation.
- **Phase 5 / Phase 6 handoff:** the scene foundation now includes explicit `SceneCatalog`/`SceneId`, headless/core/presentation separation, scene-owned Transform and mutation state, per-view Frustum state, an instance-owned behavior registry, durable `SerializedEntityId` identity, `AssetId` references, and a canonical right-handed / 0..1-depth camera convention. The previously split `SceneSerializer`/`SceneStorage`/`SceneToolingBridge`/`SceneSyncTracker` experiment is now archived under **top-level `Deprecated/`**, with no active includes/build participation: Scene creates none of it, performs no automatic scene JSON save/delta work, and sends no external-editor IPC. Critical-path items **22 through 38 are implemented**: PhysX and Jolt sit behind the same generic physics seam and parity contract; Slang is now the only first-party shader source language and generates both Vulkan SPIR-V and the isolated legacy OpenGL GLSL compatibility artifacts; the backend-neutral `Swim::Rhi` type/object contract defines formats, resource states, descriptor/capability vocabulary, adapters/devices/queues/swapchains, command objects, GPU resources, shader/pipeline objects, synchronization, and query pools without Vulkan types; an explicit runtime `GraphicsFactory` owns RHI backend registration/creation; and `Swim::RhiVulkan` now owns the Vulkan 1.3 instance/adapter/device/queue bootstrap through namespaced volk + vk-bootstrap plus SDL3-owned Vulkan WSI/swapchain presentation. `Swim::RhiVulkan` now also owns normal buffer/image allocation through VMA v3.4.0, with backend-neutral memory preferences mapped to VMA policy and VMA allocation names retained for diagnostics. Item 38 adds the reusable backend-neutral `FrameContextRing`, per-frame command-pool/list ownership, monotonic queue timeline submission, timeline-waited frame reuse, and deferred `RhiObject` retirement; the Vulkan backend now implements timeline semaphores, synchronization2 queue submission, and command-pool/list lifecycle. Swapchain replacement no longer performs a device-wide idle: it waits the supplied frame timeline and then uses a presentation-queue-only WSI completion fallback because core Vulkan presentation completion is not represented by the render timeline. **Item 39 remains the next critical-path completion gate.** Its clear/transfer implementation is now in place (see the 2026-09-05 Phase 9 checkpoint): synchronization2 barriers, dynamic-rendering clears, CPU buffer access, buffer/image copies, and an opt-in real-driver clear/readback/presentation smoke. Slang-backed procedural/indexed triangle drawing is implemented in the following Phase 9 checkpoint. Reflected descriptor tables, samplers, and sampled 2D texture drawing are now implemented in the following Phase 9 texture checkpoint. Windows/Linux GPU validation and resize/minimize/restore coverage remain open. Do not spend that runway over-polishing the current BVH/scene/GPU-dirty machinery that the later renderer/GPU Scene phases are expected to replace.
- **Testing:** the whole runnable test corpus is one program, `SwimTests`, built from self-registering suites under `Source/Tests/Suites/<group>/`. Adding coverage is a new `.cpp` in the right dependency group, never a new CMake target. The Windows clean/soft builds and the Linux builds run the complete suite, so coverage is continuously exercised instead of depending on a hand-maintained list of phase gate targets. Per-module public-header compile gates stay separate because their value is their narrow link surface. See section 32.
- **Shipping asset policy:** development auto-cook is intentionally convenient and currently enabled by default when `SwimAssetCompiler` exists. Shipping/release packaging is intended to disable `SWIM_ENABLE_DEV_ASSET_AUTOCOOK`, pre-cook with `SwimAssetCooker`, and run from compiled `.sasset`/future `.spack` data without glTF/Draco/WebP source-import code. Final packaging presets and `.spack`/memory-mapped streaming are later work, so do not confuse the current development executable with the final shipping dependency closure.

Companion documentation for the current generated solution and asset pipeline:

- `docs/VisualStudioProjectStructure.md` — what the projects/folders in the generated Visual Studio solution mean, how they depend on one another, and what a normal build actually compiles.
- `docs/SassetCookPipeline.md` — the source -> import -> cook -> `.sasset` -> runtime path, including development auto-cook versus release/shipping usage.

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
| Modern graphics backend | `Systems/Renderer/RHI/Backends/Vulkan` | RHI clear/transfer checkpoint exists. Item 39 still needs triangle/textured pipelines and Windows/Linux GPU validation. It is not yet the game renderer. |
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
- skins/skeletons, animation channels, morph targets, and camera/light import remain explicit future importer expansion and are not silently discarded under a claimed-support flag;
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

- [x] nodes/hierarchy;
- [x] transforms;
- [x] meshes/primitives;
- [x] material slots;
- [x] metallic-roughness materials;
- [x] textures/samplers;
- [ ] skins/skeletons;
- [ ] animation channels;
- [ ] morph targets;
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
- adopt reverse-Z when the modern depth pipeline is established, before HZB/occlusion code depends on depth semantics.

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
- Item 36 is implemented without leaking SDL or OS-native handles into the RHI: `Swim::Platform` owns an internal opaque Vulkan-WSI bridge that acquires/releases SDL's Vulkan loader, exposes SDL's required instance-extension list, filters queue families through SDL presentation support, and creates/destroys surfaces for `Platform::Window`. `Swim::RhiVulkan` consumes that bridge, revalidates the selected graphics queue against each concrete surface, and owns a baseline SDR swapchain path with image/image-view wrappers, acquire/present, configurable FIFO vs mailbox/immediate fallback presentation, resize/recreate, and the binary semaphore/fence primitives required by WSI. The original item-36 implementation used a temporary `vkDeviceWaitIdle` resize stopgap; item 38 has now removed that device-wide wait. HDR/minimized-window hardening remains intentionally deferred to the later swapchain/HDR validation checkpoints rather than being hidden inside item 36.
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

- [x] device-local allocation policy; *(normal RHI buffers/images now use VMA automatic device/host preference policy)*
- [ ] persistent mapped upload arenas/rings;
- [ ] readback arenas;
- [ ] transient frame buffers via render graph;
- [ ] memory-budget reporting; *(the capability/extension is wired into VMA, but allocator budget telemetry is not yet surfaced through a renderer-facing API)*
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
- [ ] minimized/zero-size window handling;
- [x] SDR baseline;
- [ ] HDR capability path;
- [x] present mode configuration;
- [x] multiple swapchains/windows supported at RHI object/lifetime level.

### Validation and diagnostics

- [x] validation requested in Debug builds;
- [ ] Vulkan object names;
- [ ] command labels;
- [ ] adapter/driver info logging;
- [ ] device-loss diagnostics;
- [ ] RenderDoc-friendly markers;
- [ ] GPU timestamps.

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
- [ ] Run clear/triangle/texture with clean Vulkan validation diagnostics on Windows and Linux, including resize/minimize/restore coverage, before closing item 39 or starting RenderGraph.

**Explicit baseline limits:** barriers and image/rendering work currently require the graphics queue family. This does not implement queue-family ownership transfers or dedicated-transfer image granularity policy. `CopyBuffer` can record on a transfer family, but ownership/synchronization is still explicit. Image copies are matching-dimension, matching-format uncompressed color copies between distinct resources; buffer/image copies additionally require single sampling and tightly packed rows. Compressed/depth-stencil transfers, integer color clears, independent stencil load/store, and read-only depth rendering attachments need later contract extensions; unsupported paths reject instead of silently choosing a representation. Resource states remain caller-supplied until RenderGraph tracking exists. Persistent upload/readback arenas and async residency are still items 41–44.

**Validation in this environment:** a dependency-enabled GCC 13/C++20 Debug foundation build compiles and links the real pinned Vulkan-Headers/volk/vk-bootstrap/VMA backend and both RHI public-header gates. `SwimTests` passes **85 cases / 551 checks**, including **26 RHI cases / 167 checks**. The build-layout verifier passes. This build used the real SDL/GLM/mimalloc/enkiTS dependencies with asset/shader compilers, Jolt, PhysX, and the legacy Windows engine excluded; it is not a full Windows engine build. The real-driver smoke was attempted and stops at SDL initialization with `No available video device`, before any GPU validation. Windows/MSVC and actual GPU pixel/presentation validation therefore remain pending.

To run the current clear/transfer and triangle smoke on a desktop with the full required Vulkan feature baseline, configure with `SWIM_BUILD_SHADER_COMPILER=ON`, build the Debug `SwimTests` target and enable the opt-in cases. The normal Windows/Linux presets already enable the shader compiler. Run with the Vulkan validation layers installed; inspect validation output as well as pixel checks. This now includes reflected sampled-texture readback and table replacement. Resize/minimize/restore coverage remains a separate pending gate.

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
- [x] Create pipeline layouts from program-owned reflection. The initial triangle path required an empty descriptor/push-constant interface. The texture checkpoint below adds fixed-count descriptor layouts; push constants remain unsupported. The program must outlive its layout because `GetProgram()` returns a reference.
- [x] Create dynamic-rendering graphics pipelines with vertex/fragment stages, topology, culling/winding, depth/stencil state, color blending/write masks, sample count, and dynamic viewport/scissor. Attachment formats/features and device sample limits are checked. Wireframe, depth clamp, and differing per-attachment blend states are rejected because those optional device features are not enabled. Pipelines own their attachment signatures and remain valid after creation-time shader modules/layouts are destroyed under Vulkan 1.3.
- [x] Record direct and indexed draws. Drawing requires the graphics queue, an active rendering scope, a bound same-device pipeline, viewport/scissor, and matching attachment formats/sample count. Index binding validates usage, type, alignment, offset and remaining draw range. Pool reuse clears cached pipeline/dynamic/index state. CPU command checks do not inspect GPU index contents or infer resource barriers.
- [x] Keep the implementation in focused `Pipelines/VulkanShaderProgram`, `Pipelines/VulkanPipelineLayout`, `Pipelines/VulkanGraphicsPipeline`, `Internal/VulkanPipelineUtils`, and `Commands/VulkanCommandListDraw` files. Device methods delegate creation; generic RHI headers expose no Vulkan or compiler types.
- [x] Add eight native-dispatch capture tests for shader lifetime/failure cleanup, reflection ownership and rejection, graphics state translation, pipeline failure cleanup, invalid descriptions, draw prerequisites/reset, attachment/device mismatch, and index bounds.
- [x] Add `Source/Shaders/Slang/RhiSmoke/Triangle.slang`, compiled by the existing pinned Slang rules. `SwimRhiSmokeShaders` is a build-only artifact target consumed by `SwimTests`, with no new test executable or runtime compiler dependency.
- [x] Add opt-in `RHI.Vulkan.Smoke.TrianglePixelsAndIndexedParity`: create an RGBA8 target, render/read back a procedural triangle, check interior/background pixels and +Y-up orientation, then render with a 16-bit index buffer and require byte-identical pixels. Missing shader compilation or desktop/GPU support fails the opted-in test.
- [x] Implement reflected fixed-count descriptor layouts/tables/writes and samplers, then upload and sample a 2D texture through this pipeline path. See the texture checkpoint below.
- [ ] Add explicit vertex-input layout contracts/bindings as needed; the current graphics path uses shader-generated vertices, and `BindVertexBuffer` remains unsupported.
- [ ] Implement push-constant recording, compute pipelines/dispatch and queries at their respective renderer consumers; these are still explicit unsupported operations.
- [ ] Run the real clear/triangle/texture smoke with clean validation on Windows and Linux, including resize/minimize/restore, before closing item 39 and starting RenderGraph.

**Validation:** the GCC 13/C++20 Debug foundation build passes **101 cases / 688 checks**, including all eight new pipeline/draw cases and the existing shader compiler/reflection cases. Platform/Input, RHI, Vulkan factory, and ShaderCompiler public-header gates compile. The actual SHA-256-verified Slang `2026.16.1` SDK compiles the new shader to SPIR-V with `vertexMain` and `fragmentMain` entry points. The architecture verifier passes. Both opted-in real-driver smoke cases were attempted and fail at SDL initialization (`No available video device`) before GPU work; no GPU pixel, validation-layer, or full Windows/MSVC engine success is claimed. Asset compiler and concrete physics backends were excluded from this foundation build.

**Delivery:** use the complete repository ZIP in a fresh directory. Retired sources already reside under top-level `Deprecated/`, outside every active source glob. The previous delivery-specific deletion scripts, move ledger, and CMake tombstones are removed. Normal build scripts remain the only build workflow. Build outputs, dependency caches and Python bytecode are not part of the source archive.

Implementation references: [Khronos graphics pipeline creation](https://docs.vulkan.org/refpages/latest/refpages/source/VkGraphicsPipelineCreateInfo.html), [dynamic-rendering pipeline formats](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineRenderingCreateInfo.html), and [draw command requirements](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdDraw.html).

### RHI reflected texture implementation checkpoint — 2026-09-05

This checkpoint continues item **39** from the latest uploaded repository, preserving its MSVC conforming-preprocessor test fix and command-parser regression coverage. The modern RHI now has the clear, procedural/indexed triangle, and sampled 2D texture implementation paths. Item 39 remains unchecked until real Windows/Linux validation and window-lifecycle coverage pass.

- [x] Create descriptor-set layouts from program-owned schemas. Sparse spaces produce valid empty set layouts for gaps. Validate duplicate spaces/bindings, positive fixed counts, supported descriptor types, explicit vertex/fragment visibility, physical-device per-stage/aggregate limits, and native layout support before creation. Partially created layouts are released on failure.
- [x] Retain native pipeline-layout/set-layout ownership through a shared internal state held by graphics pipelines and descriptor tables. Native layout handles remain alive for the full pipeline/table lifetime. Public `PipelineLayout::GetProgram` and `DescriptorTable::GetLayout` still require those originating public objects to outlive the referring object; no dangling-reference ownership contract is implied.
- [x] Implement descriptor-table allocation for samplers, sampled textures, uniform buffers, and read-only storage buffers. Each table initially owns one descriptor pool/set; pool pooling and bindless allocation policy remain later work. Unsupported writable storage, acceleration-structure, variable-count and partially-bound descriptors reject explicitly.
- [x] Validate an entire write batch before updating any native descriptor or initialization state. Check reflected binding/array bounds, exactly one resource of the expected type, same-device ownership, buffer usage/alignment/range limits, and sampled image usage/view/sample/format compatibility. `BufferRange == 0` resolves to the remaining buffer range. Sampled views currently require single-sampled, filterable floating/normalized **2D color**; depth, integer, array-view and multisample sampling need later contract coverage.
- [x] Make tables immutable after their first recorded binding. Require every array element initialized before binding, then permit read-only reuse across command lists. Resource changes allocate a new table; retain the old table/resources until GPU completion through normal frame retirement. Writes and first binding require external host synchronization. This prevents ordinary descriptor updates from invalidating already-recorded Vulkan command buffers without introducing update-after-bind flags prematurely.
- [x] Implement normalized-coordinate samplers with nearest/linear min/mag/mip filtering, repeat/mirror/clamp addressing, finite supported LOD/bias, transparent-black float border, and bounded native sampler allocation accounting. Native failures release their reservation. Anisotropy and comparison sampling reject until the corresponding device/resource contracts are implemented.
- [x] Bind complete same-device tables to the currently selected graphics pipeline. Tables must originate from the same layout identity and match the requested space. Pipeline binding clears cached table bindings; all nonempty reflected spaces must be rebound before drawing. Pool reset also clears the command's descriptor state. Image/buffer barriers remain explicit caller work.
- [x] Add a tool-side `BuildRhiShaderInterface` conversion from parsed Slang reflection into owned RHI schemas. It preserves compiler set/binding numbers and conservatively exposes globals to the program's vertex/fragment stages. Supported flat resources are samplers, float32 2D sampled textures, uniform buffers, and read-only structured/byte-address buffers. Nested parameter blocks, descriptor-array type expansion, push constants, unsupported shader stages/access/shapes and duplicate slots return an error with no partial interface. Slang/simdjson/compiler code remains outside the runtime RHI target.
- [x] Add `RhiSmoke/Texture.slang`, compiled through the existing pinned Slang artifact target. `SwimTests` reads its actual reflection sidecar to construct the layout; descriptor positions are not duplicated in the smoke's C++ setup.
- [x] Add opt-in `RHI.Vulkan.Smoke.ReflectedTexturesAndTableReplacement`: upload two distinct 2x2 RGBA patterns, use separate immutable tables with sparse set 2, render each to a 16x16 target, read back after timeline completion, and compare every channel of every pixel against the expected nearest-sampled texels. This checks resource replacement and texture orientation without the legacy renderer or asset pools.
- [x] Add ten normal test cases across descriptor/sampler native capture and shader-interface conversion, including actual compiled reflection, failure cleanup, sparse sets, array initialization, write-batch rejection, buffer bounds/device ownership, draw prerequisites, immutability and retained native layout lifetime. Existing cases are updated for the newly supported descriptor layouts.
- [ ] Run clear/triangle/texture with clean Vulkan validation on Windows and Linux. Add resize/minimize/restore coverage, then close item 39 before starting RenderGraph.
- [ ] Extend resource reflection and binding contracts for nested parameter blocks, descriptor arrays/bindless policy, additional sampled-image shapes/numeric classes, writable storage, push constants, explicit vertex inputs and compute/query consumers when their roadmap work begins. The current Slang sidecar does not encode every shader/sampler semantic; callers must still provide shaders and resources compatible with this baseline.

**Validation:** a real GCC 13/C++20 Debug foundation build passes **111 cases / 783 checks**, including the ten new cases. The pinned Slang `2026.16.1` compiler builds the texture SPIR-V and reflection sidecar; the default suite successfully converts that actual sidecar. Platform/Input, generic RHI, Vulkan factory and ShaderCompiler public-header gates compile, and the architecture verifier passes. All three opted-in GPU smoke cases were attempted and stop at SDL initialization (`No available video device`) before native GPU execution. Full Windows/MSVC engine and GPU pixel/validation success are not claimed here; the latest upload's Windows test-build fixes are preserved. Asset compiler and concrete physics backend builds were excluded from this foundation run.

**Delivery:** complete source repository ZIP for extraction into a fresh directory; normal build scripts only. No cleanup scripts, move ledger, CMake tombstones, dependency caches or build artifacts are added. The retired archive remains outside all active build globs.

Implementation references: [Khronos descriptor update lifetime rules](https://docs.vulkan.org/refpages/latest/refpages/source/vkUpdateDescriptorSets.html), [descriptor-set layout creation](https://docs.vulkan.org/refpages/latest/refpages/source/VkDescriptorSetLayoutCreateInfo.html), and [sampler creation](https://docs.vulkan.org/refpages/latest/refpages/source/VkSamplerCreateInfo.html).

### Phase 9 exit criteria

- [ ] clear/triangle/texture test on Windows and Linux.
- [ ] validation clean.
- [x] VMA used for normal buffer/image allocation.
- [ ] resize/minimize/restore loop is stable.
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
39. [ ] Validation-clean RHI clear/triangle/texture on Windows and Linux. *(Clear/transfer and Slang procedural/indexed triangle pipelines plus opt-in pixel/presentation smoke are implemented. Reflected fixed-count descriptors, samplers and sampled 2D texture drawing are implemented. Real Windows/Linux validation plus resize/minimize/restore remain open. See the 2026-09-05 Phase 9 checkpoints.)*

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

- [x] Rigidbody contains no PhysX/Jolt pointer;
- [x] PhysX/Jolt parity baseline is registered through the same generic API. *(Both backend suites call the shared `PhysicsBackendContract`; configurations compile/run the cases only when the corresponding backend target exists.)*

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
- [x] source import is tool-side and fastgltf-based. *(The `SwimAssetCompiler` importer owns fastgltf privately and emits a Swim-owned intermediate representation.)*
- [x] compiled runtime assets are versioned, upload-friendly, and streamable. *(`.sasset` v1 is versioned/chunked/hashed/aligned with dependency and range-addressable chunk metadata; GPU residency/upload is intentionally a later phase.)*
- [x] mesh/material/texture identity is clean and non-singleton. *(Typed independent identities live in engine-owned `AssetSystem`; legacy renderer pools remain migration adapters, not the identity model.)*
- [ ] GPU Scene is persistent and independent from EnTT.
- [ ] visibility and draw generation are GPU-driven with no CPU feedback requirement.
- [ ] GeometryHeap and bindless resources use safe deferred lifetimes.
- [ ] metallic-roughness PBR + IBL is production baseline.
- [ ] Clustered Forward+ is the standard local-light path.
- [ ] shadows/HDR/post are RenderGraph modules.
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
