# Swim Engine — Visual Studio Project Structure

> **Scope:** this document explains the generated Visual Studio solution as a set of build projects/targets. It is intentionally not a tour of every source folder. The goal is to make it obvious what each project owns, what normally compiles, and why the solution is grouped the way it is.

## 1. The important rule

The Visual Studio solution is generated from CMake. The solution folders are presentation only; the real architecture is the CMake target dependency graph.

**Since 2026-09-05, "engine module" no longer means "separate CMake target."** Earlier revisions of this document described roughly a dozen first-party static libraries (`SwimCore`, `SwimMemory`, `SwimJobs`, `SwimPlatform`, `SwimInput`, `SwimIO`, `SwimAssets`, `SwimCommands`, `SwimPhysics`, `SwimPhysicsJolt`, `SwimPhysicsPhysX`, `SwimRhi`, `SwimRhiVulkan`), each its own Visual Studio project under an "Engine Modules" solution folder. That gave the build graph strong ownership enforcement, but it meant editing the engine's own code meant working across a dozen small projects whose Solution Explorer layout did not match the folders on disk. That tradeoff was reversed: those source lists still exist and are still discovered by the same `file(GLOB_RECURSE ...)` calls, at the same points in the configure, but they are compiled directly into whichever real binary needs them — almost always `SwimEngine` — and shown there as Solution Explorer filters that mirror `Source/...` on disk (via `source_group(TREE ...)`), the same mechanism `SwimEngine` already used for its own remaining sources. A module's source list is a build-graph *concept* now, not a build-graph *target*. `Tests`, `Tools`, `Third Party`, and `Examples` were intentionally left exactly as they were — see §11 for why the header-boundary gates specifically could not follow the same collapse.

The current generated solution is arranged approximately like this:

```text
SwimEngine.sln
|
|-- SwimEngine                         main executable / current full engine runtime
|                                       (all former "Engine Modules" code lives here now,
|                                        as Source/... filters, not as separate projects)
|
|-- Tools/
|   |-- SwimAssetCompiler        asset compiler module library (now also owns Assets' sources)
|   |-- SwimAssetCooker          the same module's command-line front end
|   `-- SwimShaderCompiler       Slang reflection tooling
|
|-- Examples/
|-- Tests/
|   |-- SwimTests                the entire runnable test corpus, one program
|   `-- Header Boundary/         per-module public-header compile gates
|
|-- Third Party/
`-- CMake/
```

The screenshot may show the cooker as `SwimEngineCooker`. In the current repository the canonical CMake target is **`SwimAssetCooker`**. It is the standalone asset-cooking executable described below.

---

# 2. Root project: `SwimEngine`

**Target type:** executable
**Output:** `Swim Engine.exe` on Windows
**Role:** the current complete engine/game application and migration host — and, since the collapse described in §1, the sole home of every first-party engine module's source code.

`SwimEngine` is the startup project and the final process that assembles the reusable engine modules with the still-transitional renderer, scene, physics, game, shader, and compatibility code.

It currently compiles:

- `Source/main.cpp`;
- effectively all of `Source/Engine/...` directly — `Platform`, `Input`, `Commands`, `Memory`, `Assets` (except when development auto-cook links `SwimAssetCompiler`, see below), `Jobs`, `IO`, `EngineConfig`/`EngineState`, generic `Systems/Physics`, the enabled physics backend(s), the RHI contract headers, and the Vulkan RHI backend when `SWIM_ENABLE_VULKAN_RHI` is on — alongside the renderer/scene/game code that was never split out;
- `Source/Game/...`;
- shader source files as IDE-visible/header-only source entries.

None of that is compiled a second time by a separate module project any more. `Source_group(TREE ...)` mirrors the physical `Source/...` tree in Solution Explorer, so `Source/Engine/Physics/...` still *looks* like a coherent unit when you browse the project — it just isn't a separate `.vcxproj`.

### Current direct third-party dependencies

```text
SwimEngine
  -> SDL3                 (was private to SwimPlatform)
  -> mimalloc              (was private to SwimMemory)
  -> enkiTS                (was private to SwimJobs)
  -> glm                   (was public on SwimPhysics; also used elsewhere)
  -> volk / vk-bootstrap / VulkanMemoryAllocator   (was private to SwimRhiVulkan, when SWIM_ENABLE_VULKAN_RHI is on)
  -> Jolt                 (was private to SwimPhysicsJolt, when that backend is enabled)
  -> PhysX                (was private to SwimPhysicsPhysX, when that backend is enabled)
  -> spdlog, Vulkan, OpenGL, EnTT, nlohmann_json, stb, zstd, Basis transcoder, GLAD
```

Each of the first six used to be a `PRIVATE` dependency of a now-retired module target; they moved directly onto `SwimEngine` because that module's `.cpp` files are now `SwimEngine`'s own translation units. Nothing about *what* links what changed — only *which target* declares the link.

### Development asset auto-cook exception

When:

```text
SWIM_ENABLE_DEV_ASSET_AUTOCOOK=ON
```

and `SwimAssetCompiler` exists, `SwimEngine` links `SwimAssetCompiler` instead of compiling `Source/Engine/Assets/...` itself. `SwimAssetCompiler` now embeds Assets' sources directly (see §4.1), so `SwimEngine` compiling that same directory too would duplicate every symbol at link time; `CMakeLists.txt` excludes `Assets/` from `SwimEngine`'s own source list specifically in this case and only adds it back when auto-cook is disabled or `SwimAssetCompiler` is unavailable. This is intentional **development convenience**, allowing loose `.gltf`/`.glb` sources to be inspected, cooked, and loaded at startup.

That is not the intended shipping dependency graph. A release/shipping runtime should eventually run with development auto-cook disabled and consume prebuilt `.sasset`/`.spack` data only.

---

# 3. Former "Engine Modules" — now part of `SwimEngine`

This section used to describe a dozen separate static-library projects (`SwimCore`, `SwimMemory`, `SwimJobs`, `SwimPlatform`, `SwimInput`, `SwimCommands`, `SwimIO`, `SwimAssets`, `SwimPhysics`, `SwimPhysicsPhysX`/`SwimPhysicsJolt`, `SwimRhi`, `SwimRhiVulkan`). None of them exist as CMake targets any more. Their responsibilities and internal dependency relationships have **not** changed — only where they physically compile.

## 3.1 Core (`EngineConfig`/`EngineState`)

**Owns:** lightweight engine-wide contracts that should not know about renderer/platform implementations — `EngineConfig`, `EngineState`, the `GraphicsBackend`/`PhysicsBackend` configuration enums, and command-line/runtime backend selection contracts. No renderer implementation, no source importer, no PhysX/Jolt implementation.

## 3.2 Memory (`Source/Engine/Memory`)

**Owns:** `LinearArena`, frame arena behavior, scratch arena/scopes, transient-allocation lifetime contracts. Links `mimalloc` directly on `SwimEngine` now (was `PRIVATE Swim::Mimalloc` on the retired `SwimMemory` target). `SwimEngine` also still consumes mimalloc's allocator-override object so normal process allocation uses the pinned allocator.

## 3.3 Jobs (`Source/Engine/Jobs`)

**Owns:** the general engine task scheduler API — `JobSystem`, job handles/groups, dependencies/priorities, `ParallelFor`, main-thread/pinned work, blocking lanes, deterministic shutdown, automatic scratch scopes. Depends on Memory (now just another part of the same binary) and links `enkiTS` directly. First-party callers use Swim job types rather than enkiTS types.

## 3.4 Platform (`Source/Engine/Platform`)

**Owns:** OS/window/platform services — `PlatformSystem`, `WindowSystem`/`Window`, normalized window events, native-window escape-hatch descriptors, legacy Windows external-editor/`WM_COPYDATA` compatibility code (retained but runtime-dormant), filesystem roots/path services, mapped files, dynamic libraries, monotonic clock, thread helpers, headless startup support. Links `SDL3` directly. SDL types are implementation details and should not become normal engine API types.

## 3.5 Input (`Source/Engine/Input`)

**Owns:** normalized input state/events/action maps — keyboard/mouse/controller state, physical scan/key/button types, text/IME events, action maps, gamepad axes/hotplug, focus-aware input state. Depends on Platform (now the same binary). Engine, Scene, Behavior, camera/gizmo/UI code, and game behaviors consume `Swim::Input::InputSystem` directly; `SwimEngine` publishes input once after event pumping and before fixed/update consumers.

## 3.6 In-process commands (`Source/Engine/Commands`)

**Owns:** `Swim::Commands::CommandRegistry`. This dependency-free code dispatches current in-process engine commands. It owns no input state, file IO, external editor transport, or `Machine` lifecycle. `SwimEngine` owns the registry and injects it as an optional scene tool service. `SwimTests` covers parsing, invalid input, callback replacement, and lifetime during self-removal.

## 3.7 IO (`Source/Engine/IO`)

**Owns:** asynchronous/range-oriented runtime IO — asynchronous reads, range reads, completion dispatch, mapping/IO integration needed by future streaming. Depends on Platform and Jobs (both now the same binary).

## 3.8 Assets (`Source/Engine/Assets`)

**Owns:** the runtime asset identity/schema/loader side of the asset system — `AssetId`, typed generational `AssetHandle<T>`, `AssetDatabase`, `ContentHash`, asset load state/error/dependency graph, engine-owned `AssetSystem`, runtime `MeshAsset`, `TextureAsset`, material, sampler, and `ModelAsset` schemas, `.sasset` parser/loader, KTX2 container/runtime metadata. It deliberately **does not own glTF parsing, Draco decoding, WebP decoding, or meshoptimizer**; those stay source/compiler concerns owned by `SwimAssetCompiler` (§4.1).

This is still one of the most important boundaries in the codebase, even though it is no longer a project boundary:

```text
source formats / codecs
        |
        v
SwimAssetCompiler (Tools)
        |
        v
compiled .sasset
        |
        v
Assets code (compiled into SwimEngine, or into SwimAssetCompiler — never both)
        |
        v
runtime systems / renderer residency
```

Exactly one thing compiles `Source/Engine/Assets/...` into any given final binary: `SwimAssetCompiler` embeds it (§4.1) whenever that target exists and is linked; otherwise `SwimEngine`/`SwimTests` compile it directly. See §11 for why this rule exists.

## 3.9 Generic physics (`Source/Engine/Systems/Physics`, excluding `Backends/`)

**Owns:** the backend-neutral physics API and lifetime model — generational `BodyHandle`, `ShapeHandle`, `PhysicsMaterialHandle`, `ConstraintHandle`, and `CharacterHandle`; backend-neutral world/body/shape/material descriptors; collision layer/mask data; raycast/sweep/overlap hit types; collision/trigger events; `IPhysicsBackend`/`IPhysicsWorldBackend`; the generic `PhysicsSystem` factory/owner; the generic `PhysicsWorld` facade; the `Rigidbody` gameplay component with no native backend pointer. Links `glm` directly; still has no PhysX/Jolt implementation and no EnTT/Scene dependency. Scene/ECS synchronization is intentionally **not** part of this code — `ScenePhysicsBridge` remains in the Scene runtime layer.

## 3.10 PhysX backend (`Source/Engine/Systems/Physics/Backends/PhysX`)

**Owns:** the current PhysX 5.6.1 implementation of the Swim physics contracts — foundation/physics/dispatcher lifetime, native material/shape/body tables behind generational Swim handles, static/dynamic/kinematic body translation, primitive box/sphere/capsule shapes, forces/impulses/velocity/gravity/mass/damping, collision layer filtering, raycast/sweep/overlap translation, collision/trigger callback translation, simulation-safe deferred native actor destruction. Links `PhysX` (`Swim::PhysX`) directly and depends on the external `SwimPhysXBuild` step when present. Compiled only when `SWIM_ENABLE_PHYSX_BACKEND` is on. No caller should include PhysX implementation types merely to use physics.

## 3.11 Jolt backend (`Source/Engine/Systems/Physics/Backends/Jolt`)

Reuses the same generic backend contract and the same backend-contract test fixture unchanged, so both backends are held to one behavioural specification. Links `Jolt` (`Swim::Jolt`) directly. Compiled only when `SWIM_ENABLE_JOLT_BACKEND` is on.

## 3.12 RHI contract (`Source/Engine/Systems/Renderer/RHI`, excluding `Backends/`)

**Owns:** the backend-neutral RHI contract — `RhiTypes.h`, `RhiContracts.h`, `RhiFrameLifetime.h`, `RhiFactory.h`. This is header-only and was never anything but an include path; it does not need any special handling to be part of `SwimEngine` now, and needs no link library of its own wherever else it's used (a header-boundary gate, `SwimShaderCompiler`).

## 3.13 Vulkan RHI backend (`Source/Engine/Systems/Renderer/RHI/Backends/Vulkan`)

**Owns:** Vulkan bootstrap, resources, synchronization, commands, and the graphics pipeline implementation for the modern RHI. Links `volk`, `vk-bootstrap`, and `VulkanMemoryAllocator` directly, plus the compile definitions those need (`VMA_STATIC_VULKAN_FUNCTIONS=0`, `VMA_DYNAMIC_VULKAN_FUNCTIONS=1`, `VMA_VULKAN_VERSION=1003000`, `SWIM_VULKAN_VALIDATION=1` in Debug). Compiled only when `SWIM_ENABLE_VULKAN_RHI` is on. Third-party Vulkan types remain private to this code; they must not leak through the RHI contract headers.

---

# 4. Tools

## 4.1 `SwimAssetCompiler`

**Target type:** static library
**Role:** source/import/cook implementation shared by development auto-cook, tests, and the standalone cooker — and, since the collapse in §1, the sole compiled home of the runtime Assets module's own sources whenever this target exists.

It owns the expensive/flexible authoring side of the asset pipeline:

- fastgltf `.gltf`/`.glb` parsing;
- extension interpretation;
- compiler-side Draco decode for `KHR_draco_mesh_compression`;
- WebP source decode;
- PNG/JPEG source decode;
- KTX2/Basis source handling;
- meshoptimizer processing;
- intermediate-model representation;
- static model compilation;
- `.sasset` writing;
- development incremental cook/bootstrap logic;
- **`Source/Engine/Assets/...`** (§3.8) — embedded directly rather than linked, because `SwimAssets` no longer exists as a separate target.

Target direction:

```text
SwimAssetCompiler
  -> Swim::AssetCompilerDependencies   private (fastgltf, meshoptimizer, Draco, compiler-side stb, libwebp, simdjson)
```

The compiler's public API should expose Swim-owned data, not parser/codec object graphs.

## 4.2 `SwimAssetCooker`

**Target type:** executable
**Role:** run the asset cook pipeline without launching the renderer/game.

The compiler and the cooker are deliberately **one module with two build outputs**, not two projects. The cooker owns no pipeline logic of its own: it is roughly forty lines that parse an asset-root argument, initialize `AssetSystem`, call `RunDevelopmentAssetBootstrap()`, and print statistics. Keeping it a separate *target* is unavoidable (a static library cannot own a `main()`), but keeping it a separate *module* would invite the cooker to grow its own copy of cook policy.

So both outputs are produced from one source tree:

```text
Source/Tools/AssetCompiler/
  *.cpp, *.h            -> SwimAssetCompiler (static library, also embeds Source/Engine/Assets/...)
  Cli/AssetCookerMain.cpp -> SwimAssetCooker (executable)
```

The library glob explicitly excludes `Cli/` so the front end's `main()` never lands in the archive. If a second asset tool is ever needed, it belongs beside `AssetCookerMain.cpp` under the same module rather than in a new `Source/Tools/<Something>` directory.

It links only through the compiler abstraction:

```text
SwimAssetCooker
  -> SwimAssetCompiler
  -> compiler dependencies transitively/private as required
```

Typical usage:

```text
SwimAssetCooker.exe Assets
```

The cooker invokes the same `RunDevelopmentAssetBootstrap()` implementation used by engine-start development auto-cook. This is intentional: manual/CI cooking and development startup should not drift into two different asset pipelines.

For shipping, this tool is expected to run **before packaging**, while the shipping runtime itself should not need it.

## 4.3 `SwimShaderCompiler`

**Target type:** static library
**Role:** build-tool library owning reflection metadata parsing. No Slang implementation library is linked into the RHI runtime.

Its only first-party include dependency is the backend-neutral RHI contract headers (§3.12), which need nothing beyond the default `Source` include directory; its real link dependency is `simdjson::simdjson` (private). It owns the tool-side `BuildRhiShaderInterface` conversion: parsed Slang metadata becomes an owned RHI descriptor schema. `SwimRhiVulkan`'s code (now compiled into `SwimEngine`, §3.13) consumes the resulting generic schema and owns native layouts, descriptor pools/sets, and samplers; it does not link the compiler or JSON parser.

---

# 5. `Tests` solution folder

This folder's organization is unchanged by the 2026-09-05 collapse — the user explicitly kept it as-is. What changed underneath it is *how* `SwimTests` gets access to former-module code: by compiling those same sources directly (mirroring `SwimEngine`), not by linking module libraries that no longer exist.

## 5.1 One program for the whole runnable corpus

Every runnable test in the engine lives in a single executable:

```text
SwimTests
```

There is no per-module test project. A `SwimEngineConfigTests`, `SwimJobSystemTests`, `SwimAssetSystemTests`, and so on used to exist as roughly twenty separate targets; each one needed its own `add_executable`, its own link list, its own `main()`, and its own line in the build scripts. That made the cost of adding coverage a CMake edit, and it made "run the tests" mean "run twenty binaries you have to remember".

The current shape is:

```text
Source/Tests/
  Framework/        the test framework and the single main()
  Suites/           the test cases, grouped by dependency
  Fixtures/         shared multi-suite helpers (header-only)
  HeaderBoundary/   per-module public-header compile gates
```

`SwimTests` is `EXCLUDE_FROM_ALL`, so a normal engine build does not compile it.

## 5.2 Adding a test

Test cases self-register through static initializers, so adding coverage never touches CMake:

1. create or open a `.cpp` under `Source/Tests/Suites/<group>/`;
2. write a case.

```cpp
#include "Engine/Assets/AssetSystem.h"
#include "Tests/Framework/Test.h"

SWIM_TEST("Assets.AssetSystem", "UnloadKeepsIdentity")
{
    Swim::Assets::AssetSystem assets;
    SWIM_REQUIRE(assets.Initialize());
    // ...
    assets.Shutdown();
}
```

The next configure globs the new file into `SwimTests`. The available checks are `SWIM_CHECK`, `SWIM_CHECK_MESSAGE`, `SWIM_CHECK_EQUAL`, `SWIM_CHECK_NEAR`, `SWIM_CHECK_THROWS`, `SWIM_FAIL`, and the aborting `SWIM_REQUIRE`/`SWIM_REQUIRE_MESSAGE`/`SWIM_REQUIRE_EQUAL` forms. `SWIM_CHECK*` records a failure and continues so one run reports every broken expectation; `SWIM_REQUIRE*` abandons the case when continuing would crash or produce meaningless follow-on failures.

**These checks are not `assert()`.** Swim defines `NDEBUG` in every configuration, including Debug, so `assert()` compiles to nothing everywhere. Test code must never use it.

## 5.3 Suite groups are dependency groups

The directory a suite lives in decides which configurations compile it:

| Group directory | Compiled when | Gets module code via |
| --- | --- | --- |
| `Suites/Core`, `Memory`, `Jobs`, `IO`, `Input`, `Commands` | always / when platform deps available | `SwimTests` compiling those sources directly |
| `Suites/Assets` | always | `SwimTests` compiling Assets directly, or `SwimAssetCompiler` when that target exists (never both — see §11) |
| `Suites/Physics/Generic` | always (when not offline-stubbed) | `SwimTests` compiling the generic physics sources directly, links `glm` |
| `Suites/Scene/Headless` | always | nothing renderer-facing |
| `Suites/AssetCompiler` | `SwimAssetCompiler` exists | linking `Swim::AssetCompiler`, Draco |
| `Suites/Scene/Ecs` | `EnTT::EnTT` exists | EnTT, GLM |
| `Suites/RHIVulkan` | `SWIM_ENABLE_VULKAN_RHI` is on | `SwimTests` compiling the Vulkan RHI backend sources directly, links volk/vk-bootstrap/VMA |
| `Suites/Physics/PhysX` | `SWIM_ENABLE_PHYSX_BACKEND` is on | `SwimTests` compiling the PhysX backend sources directly, links `Swim::PhysX` |
| `Suites/Physics/Jolt` | `SWIM_ENABLE_JOLT_BACKEND` is on | `SwimTests` compiling the Jolt backend sources directly, links `Swim::Jolt` |

A Linux foundation configure therefore builds the portable suites and silently omits the renderer/PhysX ones, while a full Windows build gets everything. Put a new suite in the group whose dependencies it actually needs; do not widen a group to make an include resolve.

## 5.4 Running tests

```powershell
build\windows-release\SwimTests.exe                     # everything
build\windows-release\SwimTests.exe --list              # what exists
build\windows-release\SwimTests.exe --filter=Physics    # one area
build\windows-release\SwimTests.exe "Assets.Ktx2*"      # glob, positional
build\windows-release\SwimTests.exe --verbose --stop-on-failure
build\windows-release\SwimTests.exe --report=results.xml
```

Cases are identified as `<suite>.<name>`. A filter without wildcards also matches by dotted prefix, so `--filter=AssetCompiler` selects every case in every `AssetCompiler.*` suite. Other options are `--exclude`, `--list-suites`, `--repeat`, `--shuffle[=seed]`, and `--help`. The process exits `0` only when every selected case passes, and an empty selection is an error rather than a pass, so a mistyped filter in a script cannot masquerade as success.

The Windows clean/soft build scripts build and run the whole suite on every build.

## 5.5 Header-boundary gates stay separate, on purpose

The `Tests/Header Boundary` folder holds small `OBJECT` libraries, one per module:

```text
SwimPlatformPublicHeaders
SwimIoPublicHeaders
SwimAssetPublicHeaders
SwimAssetCompilerPublicHeaders
SwimShaderCompilerPublicHeaders
SwimRhiPublicHeaders
SwimRhiVulkanPublicHeaders
SwimPhysicsPublicHeaders
SwimPhysicsBackendContractCompile
```

These are not test programs, and they were deliberately **not** folded into `SwimTests`, and deliberately **not** collapsed into `SwimEngine` the way the rest of "Engine Modules" was. Their entire value is that each one compiles *only its own module's public headers* with nothing else available — proving those headers are self-contained. `SwimTests` and `SwimEngine` both end up with effectively everything compiled in, so folding a boundary gate into either would silently destroy the guarantee it exists to provide. See §11 for the full reasoning, including why most of these gates no longer need a `LINK` argument at all.

`SwimPhysicsBackendContractCompile` is the one gate that builds by default rather than being `EXCLUDE_FROM_ALL`: it is the cheapest possible guard against a physics backend leaking into the shared backend contract, which the PhysX and Jolt backends both reuse unchanged.

New gates are declared with one call:

```cmake
swim_add_header_boundary(SwimMyModulePublicHeaders
    SOURCE Source/Tests/HeaderBoundary/MyModulePublicHeaders.cpp
    LINK <third-party targets the headers actually need, if any>)
```

`LINK` used to almost always name another first-party module target. Now that those targets don't exist, `LINK` is only needed when the header genuinely requires a third-party dependency's include path (as `SwimPhysicsPublicHeaders` needs `glm::glm`); most gates need no `LINK` at all, because `${CMAKE_SOURCE_DIR}/Source` is already the default include directory every gate gets.

## 5.6 What the tests currently cover

`SwimTests` currently runs a little over one hundred cases across suites for EngineConfig, memory arenas, the job system, async IO, input, the asset system and KTX2 container, the glTF/Draco importer, meshoptimizer, KTX2 and source-image compilation, the `.sasset` format, static model compilation, the development asset pipeline, scene catalog and identity, the deferred command buffer, the behavior registry, transform tracking, per-view frustums, render conventions, generic physics handles, and the PhysX/Jolt backend contracts.

The suites are architecture enforcement as much as correctness testing: several exist specifically to prove a module behaves correctly through only its intended public surface.

---

# 6. `Examples` solution folder

Examples are also generally `EXCLUDE_FROM_ALL`. They are unchanged organizationally by the 2026-09-05 collapse, but — like `SwimTests` — they now compile the module sources they need directly instead of linking a module library, since none of `Swim::Platform`, `Swim::Core`, `Swim::Jobs`, or `Swim::Assets` exist as targets any more.

Current examples/smoke targets include things such as:

- `SwimHelloWindow` — Platform/window smoke test; compiles `Source/Engine/Platform/...` directly, links SDL3;
- `SwimHeadlessPlatform` — Platform initialization without a visible window; same treatment;
- `SwimHeadlessCoreAssets` — Core/Jobs/Assets initialization and asset use without renderer/window state; compiles Core, Memory, Jobs, and Assets sources directly, links mimalloc and enkiTS.

These are deliberately small consumers used to prove that module *code* is usable outside the monolithic `SwimEngine` executable, even though it is no longer usable as a separately-linkable *library*. Each example is its own final binary, so compiling the same module sources into more than one example (e.g. Platform into both `SwimHelloWindow` and `SwimHeadlessPlatform`) is not a duplicate-symbol risk — that only matters within a single binary (see §11).

---

# 7. `Third Party` solution folder

This folder is unchanged by the 2026-09-05 collapse. It contains dependency projects generated or imported by CMake/CPM.

Examples include groups for:

- SDL3;
- enkiTS;
- mimalloc;
- fastgltf/simdjson;
- Draco;
- WebP;
- meshoptimizer;
- zstd;
- Basis Universal;
- GLAD;
- spdlog;
- Jolt;
- PhysX;
- Vulkan RHI (volk, vk-bootstrap, VulkanMemoryAllocator);
- other backend/runtime dependencies.

These are **not first-party engine modules** just because they appear as Visual Studio projects.

The solution grouping is intended to make ownership obvious. A dependency being visible in the solution does not mean every engine project links it.

For example:

```text
SDL3      belongs behind Platform code (now compiled into SwimEngine/SwimTests/Examples)
Draco     belongs behind SwimAssetCompiler
libwebp   belongs behind SwimAssetCompiler
mimalloc  belongs behind Memory code (now compiled into SwimEngine/SwimTests/HeadlessCoreAssets)
PhysX     is still a runtime physics implementation dependency
```

---

# 8. `CMake` solution folder

Visual Studio generators create helper projects/targets for build-system operations. These are placed under `CMake` so they do not clutter the first-party engine project list.

They are build infrastructure, not engine architecture modules.

The generated solution should always be treated as disposable output. Edit `CMakeLists.txt`/`cmake/*.cmake`, then regenerate the solution; do not hand-edit the `.sln` to change ownership or dependencies.

**Regeneration is manual by design.** `CMAKE_SUPPRESS_REGENERATION` is forced on for the Visual Studio generator, so no project carries a `--check-stamp-file` custom build and `ZERO_CHECK` does nothing. This is not a cosmetic choice: CMake attaches that stamp check to *every* project, MSBuild builds projects in parallel, and a stale stamp therefore launches one concurrent CMake configure per project against a single build tree. Those configures overwrite each other's generated files and race on the shared CPM dependency caches, which shows up as `could not lock config file .git/config`, unexplained `configure_file` failures, an `MSB8066` cascade, and finally `LNK1181` for a library no project ever managed to build.

Re-run `cmake --preset windows-vs` (or either build script, which both refresh the solution) after changing CMake files.

---

# 9. What actually compiles during common workflows

## 9.1 Building the `SwimEngine` project

Building only the `SwimEngine` project causes CMake/MSBuild/Ninja to compile its dependency closure — which is now almost everything first-party, since former module code compiles directly into it.

At a high level:

```text
SwimEngine
  |
  +-- former "Engine Modules" sources, compiled directly (Core, Memory, Jobs, Platform,
  |   Input, Commands, IO, Assets*, generic Physics, enabled physics backend(s),
  |   RHI contract headers, Vulkan RHI backend when enabled)
  |
  +-- required runtime third-party targets
  |
  +-- legacy engine/game/renderer sources still owned by SwimEngine directly
  |
  `-- SwimAssetCompiler only when development auto-cook is enabled (*then Assets
      compiles there instead, not into SwimEngine -- see §2)
```

Tests/examples do not build merely because they are visible in the solution.

## 9.2 Building the default/all target

A default `cmake --build <build-dir>` / Visual Studio `ALL_BUILD` compiles all non-`EXCLUDE_FROM_ALL` targets required by that configuration.

Because `SwimAssetCooker` is a normal tool target rather than an excluded example/test, an all-target build can compile the cooker in addition to the engine.

## 9.3 Building a test or example

Selecting one test/example project builds only that target and the dependencies it requires. Building `SwimTests` compiles the whole test corpus, the former-module sources it needs, and whatever Tools targets it still links (`SwimAssetCompiler`, `SwimShaderCompiler`); use `--filter` at run time to narrow what actually executes. A header-boundary gate builds only its own module's public headers, which is the cheapest way to check that a module's public surface is still self-contained.

## 9.4 `SWIM_BUILD_LEGACY_ENGINE=OFF`

When the legacy engine is disabled, CMake stops before creating the full `SwimEngine` renderer/game executable. Foundation module sources, asset tooling, tests, and examples can still exist and compile — into `SwimTests`/the example executables directly, since `SwimEngine` itself never gets created in this mode.

This is important for Linux and headless/foundation validation while the old renderer remains Windows-only.

## 9.5 `SWIM_BUILD_ASSET_COMPILER=OFF`

The source importer/compiler and standalone cooker are not created. Runtime asset code (§3.8) still compiles directly into `SwimEngine`/`SwimTests`/`SwimHeadlessCoreAssets`, since there is no `SwimAssetCompiler` target for them to be embedded in instead. `SwimTests` still builds; its `AssetCompiler.*` suites are simply not compiled into it.

This is part of the intended eventual shipping configuration.

## 9.6 `SWIM_ENABLE_DEV_ASSET_AUTOCOOK=OFF`

The `SwimEngine` executable stops linking `SwimAssetCompiler` merely to support loose-source startup cooking, and instead compiles `Source/Engine/Assets/...` directly like every other former module.

This switch is the key boundary between:

```text
DEVELOPMENT
source assets available -> auto-cook allowed -> load compiled assets
```

and the intended:

```text
SHIPPING
pre-cooked assets only -> no glTF/Draco/WebP source importer in runtime
```

---

# 10. Current project dependency picture

The practical dependency graph did not change on 2026-09-05 — only which box each piece of code lives inside. Everything in the left-hand column below used to be its own project; all of it is now inside the `SwimEngine` box (or the `SwimTests`/example box, when that binary needs the same code and compiles it directly instead):

```text
Core           (no deps)
Memory         -> mimalloc
Jobs           -> Memory, enkiTS
Platform       -> SDL3
Input          -> Platform
Commands       (no deps)
IO             -> Platform, Jobs
Assets         (no deps of its own)
Generic Physics -> glm
PhysX backend  -> Generic Physics, PhysX
Jolt backend   -> Generic Physics, Jolt
RHI contract   (header-only, no deps)
Vulkan RHI     -> RHI contract, Platform, volk, vk-bootstrap, VulkanMemoryAllocator

SwimEngine
  -> all of the above, compiled directly (Assets only when dev auto-cook is off)
  -> SwimAssetCompiler only when dev auto-cook is on
  -> remaining legacy renderer/scene/game implementation sources/dependencies

SwimAssetCompiler (Tools)
  -> Assets (embedded), fastgltf / simdjson / meshoptimizer / Draco / stb / libwebp
  -> SwimAssetCooker (same module; Cli/ front end)

SwimShaderCompiler (Tools)
  -> RHI contract headers (no link needed), simdjson
```

Scene, Render, UI, and Audio extraction remain future work; the modern Vulkan RHI is a separate foundation subtree within `SwimEngine` now (see §3.13), and the main game renderer is still transitional.

---

# 11. Why the module collapse happened, and what stayed the same

On 2026-09-05, every target in the old "Engine Modules" solution folder (and its `RHI Backends`/`Physics Backends` subfolders) was retired as a separate CMake target. Their source lists, dependency setup, and (for the physics/RHI backends) feature gating are still discovered at exactly the same points in `CMakeLists.txt`, but the `add_library(...)` calls, `ALIAS` targets, and `swim_set_solution_folder(...)` calls for them are gone. Instead, `source_group(TREE ${CMAKE_SOURCE_DIR} ...)` — the same mechanism `SwimEngine` already used for its own unsplit sources — makes them appear as Solution Explorer filters mirroring the physical `Source/...` layout, inside whichever real target now compiles them (almost always `SwimEngine`).

Two rules made this safe:

1. **A module's sources compile into at most one place within any given final binary.** Most modules have exactly one consumer that matters (`SwimEngine`), so this is automatic. Where a binary would otherwise get a module two ways at once — `SwimTests` links `SwimAssetCompiler` *and* would otherwise want Assets directly, since `SwimAssetCompiler` now embeds Assets' sources itself — the CMake logic picks exactly one source and the other gets the code by linking that target instead. `SwimEngine` has the identical case with development auto-cook (§2).
2. **A module's own third-party `PRIVATE` dependency moved onto whichever target now compiles it.** SDL3 (Platform), mimalloc (Memory), enkiTS (Jobs), Jolt/PhysX (their respective backends), and volk/vk-bootstrap/VulkanMemoryAllocator (Vulkan RHI) are now linked directly by `SwimEngine`, `SwimTests`, or the relevant example — exactly what the retired module target used to declare, just one level up.

**Tests, Third Party, Tools, and Examples were deliberately left alone.** The header-boundary gates under `Tests/Header Boundary` are the one place this collapse could not simply apply: their entire purpose is to compile a module's public headers with *nothing else available*, proving those headers are self-contained. Folding a module into `SwimEngine` (which now compiles almost everything) would make every boundary gate meaningless if it followed along. So those gates still exist as small `OBJECT` library targets — they just needed one adjustment: since they never actually link (an `OBJECT` library compiles but doesn't produce a linked binary), most of them no longer need a `LINK` argument at all — the module aliases they used to name only ever provided the default `${CMAKE_SOURCE_DIR}/Source` include path anyway. The two gates that genuinely needed a third-party include path (`SwimPhysicsPublicHeaders`, `SwimPhysicsBackendContractCompile` — both need `glm` for the physics headers' `glm::vec3` etc.) link that third-party target directly now instead of the retired `Swim::Physics` alias.

`SwimAssetCompiler` and `SwimShaderCompiler` also stayed real Tools-folder targets, for the same reason the user gave for Tests/Third Party: those are appropriate as separate projects. `SwimAssetCompiler` picked up one new responsibility as a result — it now embeds `Source/Engine/Assets/...` directly, since `SwimAssets` no longer exists for it to link.

See architecture plan §0.2 (file organization within a module — unaffected by this change) and §4.3 (build graph and CMake invariants — updated for this collapse) for the related engine-wide rules.
