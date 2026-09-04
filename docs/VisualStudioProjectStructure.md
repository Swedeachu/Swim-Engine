# Swim Engine — Visual Studio Project Structure

> **Scope:** this document explains the generated Visual Studio solution as a set of build projects/targets. It is intentionally not a tour of every source folder. The goal is to make it obvious what each project owns, what normally compiles, and why the solution is grouped the way it is.

## 1. The important rule

The Visual Studio solution is generated from CMake. The solution folders are presentation only; the real architecture is the CMake target dependency graph.

A project should own one coherent layer. Dependencies should flow downward into lower-level modules. Source files should not be duplicated between projects just to make the solution look organized.

The current generated solution is arranged approximately like this:

```text
SwimEngine.sln
|
|-- SwimEngine                         main executable / current full engine runtime
|
|-- Engine Modules/
|   |-- SwimCore
|   |-- SwimMemory
|   |-- SwimJobs
|   |-- SwimPlatform
|   |-- SwimInput
|   |-- SwimIO
|   |-- SwimAssets
|   |-- SwimPhysics
|   `-- Physics Backends/
|       `-- SwimPhysicsPhysX
|
|-- Tools/
|   |-- SwimAssetCompiler        asset compiler module library
|   `-- SwimAssetCooker          the same module's command-line front end
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
**Role:** the current complete engine/game application and migration host.

`SwimEngine` is the startup project and the final process that assembles the reusable engine modules with the still-transitional renderer, scene, physics, game, shader, and compatibility code.

It currently compiles:

- `Source/main.cpp`;
- the remaining `Source/Engine/...` runtime sources that have not yet been extracted into their own first-party target;
- `Source/Game/...`;
- shader source files as IDE-visible/header-only source entries;
- the reusable modules below through normal target linking rather than recompiling their source files.

The module roots already extracted into separate targets are explicitly excluded from the `SwimEngine` recursive source list. For example, `Source/Engine/Platform`, `Input`, `Memory`, `Assets`, `Jobs`, `IO`, and the generic `Systems/Physics` implementation are compiled by their module projects exactly once and linked into `SwimEngine`. The PhysX implementation is compiled separately by `SwimPhysicsPhysX`.

### Current direct module dependencies

```text
SwimEngine
  -> SwimCore
  -> SwimMemory
  -> SwimAssets
  -> SwimJobs
  -> SwimIO
  -> SwimPlatform
  -> SwimInput
  -> SwimPhysics
  -> SwimPhysicsPhysX
```

The current legacy executable still directly owns transitional runtime dependencies such as Vulkan/OpenGL, GLM, EnTT, zstd, Basis transcoding, GLAD, spdlog, and some remaining stb compatibility use. PhysX is no longer one of those raw executable dependencies: it is private to `SwimPhysicsPhysX`. The remaining renderer/scene dependencies are expected to become narrower implementation-module dependencies as migration continues.

### Development asset auto-cook exception

When:

```text
SWIM_ENABLE_DEV_ASSET_AUTOCOOK=ON
```

and `SwimAssetCompiler` exists, `SwimEngine` additionally links `SwimAssetCompiler`. This is intentional **development convenience**, allowing loose `.gltf`/`.glb` sources to be inspected, cooked, and loaded at startup.

That is not the intended shipping dependency graph. A release/shipping runtime should eventually run with development auto-cook disabled and consume prebuilt `.sasset`/`.spack` data only.

---

# 3. Engine Modules

These are first-party reusable static libraries. Their presence as separate Visual Studio projects is deliberate: the build graph can enforce ownership and dependency direction instead of relying only on source-folder conventions.

## 3.1 `SwimCore`

**Target type:** static library  
**Owns:** lightweight engine-wide contracts that should not know about renderer/platform implementations.

Current contents include:

- `EngineConfig`;
- `EngineState`;
- `GraphicsBackend` configuration enum;
- `PhysicsBackend` configuration enum;
- command-line/runtime backend selection contracts.

Current dependency character:

```text
SwimCore
  -> no renderer implementation
  -> no source importer
  -> no PhysX/Jolt implementation
```

This is intentionally small. More generic core contracts may move here as the public engine boundary matures.

## 3.2 `SwimMemory`

**Target type:** static library  
**Owns:** engine memory-lifetime utilities.

Current responsibilities include:

- `LinearArena`;
- frame arena behavior;
- scratch arena/scopes;
- transient-allocation lifetime contracts.

Dependency direction:

```text
SwimMemory
  -> Swim::Mimalloc
```

The final `SwimEngine` process also consumes mimalloc's allocator-override object so normal process allocation uses the pinned allocator without exposing mimalloc APIs throughout gameplay code.

## 3.3 `SwimJobs`

**Target type:** static library  
**Owns:** the general engine task scheduler API.

Current responsibilities include:

- `JobSystem`;
- job handles/groups;
- dependencies/priorities;
- `ParallelFor`;
- main-thread/pinned work;
- blocking lanes;
- deterministic shutdown;
- automatic scratch scopes around job execution.

Dependency direction:

```text
SwimJobs
  -> SwimMemory
  -> enkiTS implementation
```

First-party callers use Swim job types rather than enkiTS types.

## 3.4 `SwimPlatform`

**Target type:** static library  
**Owns:** OS/window/platform services.

Current responsibilities include:

- `PlatformSystem`;
- `WindowSystem` / `Window`;
- normalized window events;
- native-window escape-hatch descriptors;
- legacy Windows external-editor/`WM_COPYDATA` compatibility code (retained but runtime-dormant);
- filesystem roots/path services;
- mapped files;
- dynamic libraries;
- monotonic clock;
- thread helpers;
- headless startup support.

Dependency direction:

```text
SwimPlatform
  -> SDL3 privately
```

SDL types are implementation details and should not become normal engine API types.

## 3.5 `SwimInput`

**Target type:** static library  
**Owns:** normalized input state/events/action maps.

Current responsibilities include:

- keyboard/mouse/controller state;
- physical scan/key/button types;
- text/IME events;
- action maps;
- gamepad axes/hotplug;
- focus-aware input state.

Dependency direction:

```text
SwimInput
  -> SwimPlatform
```

The old gameplay-facing `InputManager` still exists as a compatibility adapter around this system. Removing that remaining adapter from Scene/Behavior/gameplay APIs is separate migration work.

## 3.6 `SwimIO`

**Target type:** static library  
**Owns:** asynchronous/range-oriented runtime IO.

Current responsibilities include:

- asynchronous reads;
- range reads;
- completion dispatch;
- mapping/IO integration needed by future streaming.

Dependency direction:

```text
SwimIO
  -> SwimPlatform       public
  -> SwimJobs           private
```

The public IO contract can use platform-owned path/file concepts while scheduling details stay private.

## 3.7 `SwimAssets`

**Target type:** static library  
**Owns:** the runtime asset identity/schema/loader side of the asset system.

Current responsibilities include:

- `AssetId`;
- typed generational `AssetHandle<T>`;
- `AssetDatabase`;
- `ContentHash`;
- asset load state/error/dependency graph;
- engine-owned `AssetSystem`;
- runtime `MeshAsset`, `TextureAsset`, material, sampler, and `ModelAsset` schemas;
- `.sasset` parser/loader;
- KTX2 container/runtime metadata.

It deliberately **does not own glTF parsing, Draco decoding, WebP decoding, or meshoptimizer**. Those are source/compiler concerns and belong to `SwimAssetCompiler`.

This separation is one of the most important project boundaries in the solution:

```text
source formats / codecs
        |
        v
SwimAssetCompiler
        |
        v
compiled .sasset
        |
        v
SwimAssets
        |
        v
runtime systems / renderer residency
```


## 3.8 `SwimPhysics`

**Target type:** static library  
**Owns:** the backend-neutral physics API and lifetime model.

Current responsibilities include:

- generational `BodyHandle`, `ShapeHandle`, `PhysicsMaterialHandle`, `ConstraintHandle`, and `CharacterHandle`;
- backend-neutral world/body/shape/material descriptors;
- collision layer/mask data;
- raycast/sweep/overlap hit types;
- collision/trigger events;
- `IPhysicsBackend` / `IPhysicsWorldBackend`;
- the generic `PhysicsSystem` factory/owner;
- the generic `PhysicsWorld` facade;
- the `Rigidbody` gameplay component with no native backend pointer.

Dependency direction:

```text
SwimPhysics
  -> glm                 public math contract
  -> no PhysX/Jolt implementation
  -> no EnTT/Scene dependency
```

Scene/ECS synchronization is intentionally **not** part of this target. `ScenePhysicsBridge` remains in the Scene runtime layer and consumes `SwimPhysics`, keeping EnTT out of the generic physics API.

## 3.9 `SwimPhysicsPhysX`

**Target type:** static library  
**Solution folder:** `Engine Modules/Physics Backends`  
**Owns:** the current PhysX 5.6.1 implementation of the Swim physics contracts.

Current responsibilities include:

- PhysX foundation/physics/dispatcher lifetime;
- native material/shape/body tables behind generational Swim handles;
- static/dynamic/kinematic body translation;
- primitive box/sphere/capsule shapes;
- forces, impulses, velocity, gravity, mass, and damping;
- collision layer filtering;
- raycast, sweep, and overlap translation;
- collision/trigger callback translation;
- simulation-safe deferred native actor destruction.

Dependency direction:

```text
SwimPhysicsPhysX
  -> SwimPhysics         public
  -> Swim::PhysX         private
```

No caller should include PhysX implementation types merely to use physics. The future `SwimPhysicsJolt` target is expected to implement the same generic contract and reuse the same backend contract test suite.

---

# 4. Tools

## 4.1 `SwimAssetCompiler`

**Target type:** static library  
**Role:** source/import/cook implementation shared by development auto-cook, tests, and the standalone cooker.

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
- development incremental cook/bootstrap logic.

Target direction:

```text
SwimAssetCompiler
  -> SwimAssets                         public
  -> Swim::AssetCompilerDependencies   private
```

The private compiler dependency bundle currently owns the source-format implementation dependencies such as simdjson/fastgltf, meshoptimizer, Draco, compiler-side stb, and libwebp.

The compiler's public API should expose Swim-owned data, not parser/codec object graphs.

## 4.2 `SwimAssetCooker`

**Target type:** executable  
**Role:** run the asset cook pipeline without launching the renderer/game.

The compiler and the cooker are deliberately **one module with two build outputs**, not two projects. The cooker owns no pipeline logic of its own: it is roughly forty lines that parse an asset-root argument, initialize `AssetSystem`, call `RunDevelopmentAssetBootstrap()`, and print statistics. Keeping it a separate *target* is unavoidable (a static library cannot own a `main()`), but keeping it a separate *module* would invite the cooker to grow its own copy of cook policy.

So both outputs are produced from one source tree:

```text
Source/Tools/AssetCompiler/
  *.cpp, *.h            -> SwimAssetCompiler (static library)
  Cli/AssetCookerMain.cpp -> SwimAssetCooker (executable)
```

The library glob explicitly excludes `Cli/` so the front end's `main()` never lands in the archive. If a second asset tool is ever needed, it belongs beside `AssetCookerMain.cpp` under the same module rather than in a new `Source/Tools/<Something>` directory.

It links only through the compiler abstraction:

```text
SwimAssetCooker
  -> SwimAssetCompiler
  -> SwimAssets
  -> compiler dependencies transitively/private as required
```

Typical usage:

```text
SwimAssetCooker.exe Assets
```

The cooker invokes the same `RunDevelopmentAssetBootstrap()` implementation used by engine-start development auto-cook. This is intentional: manual/CI cooking and development startup should not drift into two different asset pipelines.

For shipping, this tool is expected to run **before packaging**, while the shipping runtime itself should not need it.

---

# 5. `Tests` solution folder

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

| Group directory | Compiled when | Links |
| --- | --- | --- |
| `Suites/Core`, `Memory`, `Jobs`, `IO`, `Input`, `Assets` | always | the foundation modules |
| `Suites/Physics/Generic` | always | `Swim::Physics` |
| `Suites/Scene/Headless` | always | nothing renderer-facing |
| `Suites/AssetCompiler` | `SwimAssetCompiler` exists | `Swim::AssetCompiler`, Draco |
| `Suites/Scene/Ecs` | `EnTT::EnTT` exists | EnTT, GLM |
| `Suites/Physics/PhysX` | `SwimPhysicsPhysX` exists | `Swim::PhysicsPhysX` |

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
SwimPhysicsPublicHeaders
SwimPhysicsBackendContractCompile
```

These are not test programs, and they were deliberately **not** folded into `SwimTests`. Their entire value is that each one links only its own module: `SwimPhysicsPublicHeaders` proves the generic physics headers compile and link against `Swim::Physics` with no backend present. `SwimTests` links everything, so moving these into it would silently destroy the guarantee they exist to provide.

`SwimPhysicsBackendContractCompile` is the one gate that builds by default rather than being `EXCLUDE_FROM_ALL`: it is the cheapest possible guard against a physics backend leaking into the shared backend contract, which is the fixture a future `SwimPhysicsJolt` will reuse unchanged.

New gates are declared with one call:

```cmake
swim_add_header_boundary(SwimMyModulePublicHeaders
    SOURCE Source/Tests/HeaderBoundary/MyModulePublicHeaders.cpp
    LINK Swim::MyModule)
```

## 5.6 What the tests currently cover

`SwimTests` currently runs a little over one hundred cases across suites for EngineConfig, memory arenas, the job system, async IO, input, the asset system and KTX2 container, the glTF/Draco importer, meshoptimizer, KTX2 and source-image compilation, the `.sasset` format, static model compilation, the development asset pipeline, scene catalog and identity, the deferred command buffer, the behavior registry, transform tracking, per-view frustums, render conventions, generic physics handles, and the PhysX backend contract.

The suites are architecture enforcement as much as correctness testing: several exist specifically to prove a module behaves correctly through only its intended public surface.

---

# 6. `Examples` solution folder

Examples are also generally `EXCLUDE_FROM_ALL`.

Current examples/smoke targets include things such as:

- `SwimHelloWindow` — Platform/window smoke test;
- `SwimHeadlessPlatform` — Platform initialization without a visible window;
- `SwimHeadlessCoreAssets` — Core/Jobs/Assets initialization and asset use without renderer/window state.

These are deliberately small consumers used to prove that modules are usable outside the monolithic `SwimEngine` executable.

---

# 7. `Third Party` solution folder

This folder contains dependency projects generated or imported by CMake/CPM.

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
- PhysX;
- other backend/runtime dependencies.

These are **not first-party engine modules** just because they appear as Visual Studio projects.

The solution grouping is intended to make ownership obvious. A dependency being visible in the solution does not mean every engine project links it.

For example:

```text
SDL3      belongs behind SwimPlatform
Draco     belongs behind SwimAssetCompiler
libwebp   belongs behind SwimAssetCompiler
mimalloc  belongs behind SwimMemory/process allocation
PhysX     is still a runtime physics implementation dependency
```

---

# 8. `CMake` solution folder

Visual Studio generators create helper projects/targets for build-system operations. These are placed under `CMake` so they do not clutter the first-party engine project list.

They are build infrastructure, not engine architecture modules.

The generated solution should always be treated as disposable output. Edit `CMakeLists.txt`/`cmake/*.cmake`, then regenerate the solution; do not hand-edit the `.sln` to change ownership or dependencies.

---

# 9. What actually compiles during common workflows

## 9.1 Building the `SwimEngine` project

Building only the `SwimEngine` project causes CMake/MSBuild/Ninja to compile its dependency closure.

At a high level:

```text
SwimEngine
  |
  +-- first-party engine modules
  |
  +-- required runtime third-party targets
  |
  +-- legacy engine/game/renderer sources still owned by SwimEngine
  |
  `-- SwimAssetCompiler only when development auto-cook is enabled
```

Tests/examples do not build merely because they are visible in the solution.

## 9.2 Building the default/all target

A default `cmake --build <build-dir>` / Visual Studio `ALL_BUILD` compiles all non-`EXCLUDE_FROM_ALL` targets required by that configuration.

Because `SwimAssetCooker` is a normal tool target rather than an excluded example/test, an all-target build can compile the cooker in addition to the engine.

## 9.3 Building a test or example

Selecting one test/example project builds only that target and the dependencies it requires. Building `SwimTests` compiles the whole test corpus and the modules it links; use `--filter` at run time to narrow what actually executes. A header-boundary gate builds only its own module, which is the cheapest way to check that a module's public surface is still self-contained.

## 9.4 `SWIM_BUILD_LEGACY_ENGINE=OFF`

When the legacy engine is disabled, CMake stops before creating the full `SwimEngine` renderer/game executable. Foundation modules, asset tooling, tests, and examples can still exist.

This is important for Linux and headless/foundation validation while the old renderer remains Windows-only.

## 9.5 `SWIM_BUILD_ASSET_COMPILER=OFF`

The source importer/compiler and standalone cooker are not created. `SwimAssets` remains available because runtime asset loading is a separate module. `SwimTests` still builds; its `AssetCompiler.*` suites are simply not compiled into it.

This is part of the intended eventual shipping configuration.

## 9.6 `SWIM_ENABLE_DEV_ASSET_AUTOCOOK=OFF`

The `SwimEngine` executable stops linking `SwimAssetCompiler` merely to support loose-source startup cooking.

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

The first-party projects currently form this practical graph:

```text
                    +--------------------+
                    |     SwimCore       |
                    +--------------------+

+-------------+       +-------------+
| SwimMemory  | ----> |  mimalloc   |
+------+------+       +-------------+
       ^
       |
+------+------+
|  SwimJobs   | ----> enkiTS
+-------------+

+--------------+ ----> SDL3
| SwimPlatform |
+------+-------+
       ^
       +----------------+
       |                |
+------+-----+     +----+------+
| SwimInput  |     |  SwimIO   |
+------------+     +-----+-----+
                         |
                         +----> SwimJobs

+-------------+
| SwimAssets  |
+------+------+ 
       ^
       |
+------+-------------+
| SwimAssetCompiler  | ----> fastgltf / simdjson / meshoptimizer
+------+-------------+ ----> Draco / stb / libwebp
       |
       +----> SwimAssetCooker  (same module; Cli/ front end)
       |
       `----> SwimEngine only when dev auto-cook is enabled

+-------------+
| SwimPhysics | ----> GLM
+------+------+
       ^
       |
+------+------------+
| SwimPhysicsPhysX  | ----> PhysX (private)
+-------------------+

SwimEngine
  -> Core + Memory + Jobs + IO + Platform + Input + Assets
  -> SwimPhysics + SwimPhysicsPhysX
  -> remaining legacy renderer/scene/game implementation targets/dependencies
```

This is not yet the final engine graph. Future work will add the Jolt physics backend and extract Scene, RHI, Vulkan RHI, Render, UI, Audio, etc. into similarly explicit targets. The generic Physics API and current PhysX backend are already separate targets, so future backend parity should not require putting native physics types back into the root executable.

---

# 11. Practical rules when adding a new project

1. Create a target because it owns a real architectural layer, not merely because a folder has many files.
2. Keep implementation libraries private whenever callers do not need their types.
3. Public target links are part of the API contract; use them deliberately.
4. Do not add a dependency directly to `SwimEngine` just because that is the easiest way to fix an include/link error.
5. Do not compile the same first-party `.cpp` file into both a module and `SwimEngine`.
6. Tests/examples should normally be `EXCLUDE_FROM_ALL` unless they are intentionally part of normal production build output.
7. Do not create a new test target. Runnable coverage goes into a suite file under `Source/Tests/Suites/<group>/`; a new target is only justified when the point of the check is a narrower link surface than `SwimTests` has, which means a header-boundary gate.
8. A tool that owns a `main()` belongs in its module's `Cli/` directory, not in a new module. The extra target is a link-time necessity, not a second owner of the logic.
9. Source import/codec libraries belong to tooling targets; runtime-format readers belong to `SwimAssets`.
10. The generated Visual Studio solution should make the ownership graph easier to read, but CMake remains the source of truth.
