# Engine cleanup — 2026-09-30

The cooked-model runtime importer now belongs to `Engine`, under `Source/Engine/Systems/Scene/ModelImporter/`. It instantiates mesh/material assets into scene entities and remains separate from the offline glTF/Draco/Basis asset cooker. Sponza's selection preferences are sandbox policy, expressed by calling the generic `Engine::FindCookedModel`. `Findings.cpp`, `Findings.h` and their synchronization script have been removed; historical findings remain documentation only.

Retained UI has document-owned deferred interaction callbacks, a typed widget factory registry used by engine and custom widgets, and extensible theme class builders. The sandbox and console use this API. See [TextAndUi.md](TextAndUi.md#document-callbacks-and-widget-registration-2026-09-30) for examples and lifetime rules.

## Shader registration and reflection

Build declarations are the source of truth for runtime registration, deployment and the required-program diagnostic list. `cmake/Shaders.cmake` generates `Generated/Engine/RuntimeShaderCatalog.h` from registrations; there is no manually counted C++ array or separate deployment mapping.

Existing engine compilation roots declare their runtime name and whether they gate base rendering:

```cmake
swim_add_slang_program(SwimForwardOpaque
    RUNTIME_NAME ForwardOpaque
    REQUIRED
    SOURCE Source/Shaders/Slang/ForwardPlus/ClusteredForward.slang
    PROFILE spirv_1_5
    INCLUDE_DIRECTORIES Source/Shaders/Slang
)
```

A gameplay or render-feature shader needs one call before shader-set definition/deployment:

```cmake
swim_add_runtime_shader(GameTint
    SOURCE Source/Game/Shaders/GameTint.slang
    INCLUDE_DIRECTORIES Source/Game/Shaders
)
```

Both paths use the same registry. Duplicate names and registrations after deployment has been defined fail at configure time. Feature shaders are optional unless declared `REQUIRED`. Compilation and staging retain deterministic `.spv` and `.reflection.json` artifacts and depfile tracking.

`ShaderLibrary` registers the generated catalog automatically. External artifacts can use `Register(RuntimeShaderDesc{ "GameTint", "GameTint.spv", "GameTint.reflection.json" })`; relative paths use the library root, absolute paths are accepted, and custom entry-point names are fields of that descriptor. Duplicate registration is rejected. Registration does not imply immediate GPU creation. `LoadCompute` and `LoadGraphics` use the same runtime loading path for built-in and gameplay programs.

Parsed bytecode, reflection, binding names and the RHI interface are cached per library/program. `Invalidate(name)` discards the CPU cache for future loads; existing GPU programs keep their lifetime. This is an explicit reload boundary, not live pipeline replacement. The library and renderer are owned by the render thread.

Reflection JSON is retained. Its cost is file I/O, parsing and conversion when a program is first loaded, not per-frame GPU work. Binary reflection could reduce cold-start time and distribution size, but requires a versioned schema, cooker integration and compatibility tests. The current cache addresses repeated loading without introducing that format. No frame-rate improvement from a binary conversion is claimed.

## Rendering boundaries and performance review

The RHI and Vulkan backend have no black-hole, gravitational-lensing or camera-effect-specific code. Those effects already use generic `RenderFeatureContext`, reflected bindings and RenderGraph passes. The high-level implementations remain available as native optional features; gameplay selects and configures them. Shader catalog registration describes artifacts and does not couple the backend to their gameplay meaning.

This run did not obtain a Vulkan software driver. Vulkan loader libraries are present but no ICD is installed, and the package installation attempt failed because this environment cannot perform the required package-manager credential transitions. SwiftShader GPU timing and compute occupancy measurements were therefore skipped. Existing desktop measurements in `Profiling.md` remain historical evidence; they were not rerun here.

Static review of the current code identifies these measurement targets:

| Area | Current behavior | What to measure |
| --- | --- | --- |
| Shader/program creation | Core programs are created in renderer initialization; feature compute pipelines are retained in `featurePrograms` after first use | Cold initialization and the first frame enabling an effect; programs are not all recreated every frame |
| Frame graph | Pass declaration, `graph.Compile()` and recording run each frame | Existing build/compile/executor timing zones; topology caching needs careful invalidation for settings, features, probe updates and dimensions |
| CPU/GPU synchronization | Two frame slots reuse executors after `GatherTimings` waits for the selected slot; capture additionally waits for readback | Slot wait time separately from CPU recording; screenshot readback is an explicit synchronization path |
| Descriptor/view setup | Passes create descriptor tables; backend pool and view caches already exist | Allocation, write and recording costs before changing descriptor lifetime rules |
| Clouds/lensing/screen-space work | Expensive sampling and multiple compute passes; existing resolution/step controls | GPU pass times while ablating one feature at a time, then occupancy/bandwidth on a real GPU |
| UI bindings | Per-frame gameplay watcher scans removed; callbacks are queued only for registered interactions | Input/UI CPU time with large documents and frequent interaction |

No compute-kernel rewrite or unsupported percentage-speedup estimate is included. Use the existing `profile` and `bench` commands in [Profiling.md](Profiling.md) on the desktop to determine whether the limiting work is graph recording, synchronization, bandwidth or shader execution.

## Historical archive and packaging

All historical source, shader and CMake files under `Deprecated/` end in `.txt`; Markdown descriptions remain readable. `cmake/ArchiveBoundary.cmake` checks actual target source lists after configuration and rejects archive references. The supplied tree lacked runtime fonts and the Sponza source; fonts with their license and the Sponza GLB from the project's platform branch have been restored.

The delivered ZIP contains repository source, configuration, documentation, fixtures, archive text and runtime assets. Fetched dependencies, build trees, executables, generated shaders, caches and test output are excluded.

## Validation

- Linux Release configuration and builds of `SwimEngine`, `SwimTests` and `SwimTextUiPublicHeaders` passed. This host used `SDL_UNIX_CONSOLE_BUILD=ON` because desktop development libraries are unavailable; Windows builds were not run here.
- 60 registered runtime shader programs compiled and staged, with 38 base-renderer requirements generated from registration.
- 107 focused UI/input/runtime/console/render-feature/sandbox cases passed (3,972 checks).
- 79 shader-compiler/reflection and engine model-import cases passed (3,658 checks).
- `scripts/verify-build-layout.py` passed. All 216 engine entries in `compile_commands.json` exclude `Deprecated/`.
- A negative configure fixture intentionally adding an archived source was rejected by the CMake guard.
- Offline dependency-stub configuration passed; it is structural validation only, not a substitute for the native build.

The commands used for the native build and focused runs were:

```sh
cmake -S . -B build/linux-release -DCMAKE_BUILD_TYPE=Release \
    -DSWIM_BUILD_ENGINE=ON -DSWIM_BUILD_PLATFORM_EXAMPLE=OFF \
    -DSWIM_ENABLE_PCH=OFF -DSDL_UNIX_CONSOLE_BUILD=ON
cmake --build build/linux-release --target SwimEngine SwimTests SwimTextUiPublicHeaders --parallel 3
build/linux-release/SwimTests --filter=UI --filter=UiInput --filter=Engine.UiRuntime \
    --filter=Engine.RuntimeConsole --filter=Engine.RenderFeature --filter=Engine.ShaderLibrary \
    --filter=Engine.CameraEffects --filter=Render.Ui --filter=Game.Sandbox --filter=Game.LightSwarm
build/linux-release/SwimTests --filter=Engine.ModelImport --filter=ShaderCompiler
python3 scripts/verify-build-layout.py
```

The UI and shader regressions specifically exercise deferred callback dispatch, polling independence, node removal, callback unregistration, silent setters, stable custom theme IDs, transactional theme replacement, typed custom factories, built-in composite factories, artifact registration, duplicate rejection and explicit cache invalidation.
