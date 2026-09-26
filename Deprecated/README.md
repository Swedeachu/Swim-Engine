# Retired code

Nothing under this directory is compiled, linked, packaged as runtime code, or included by active `Source/` files. The build files mention this directory only in comments, and `scripts/verify-build-layout.py` enforces this. These files keep historical implementations for reference. They are not compatibility modules, and they are not a second way to reach current services.

Imports and APIs in the archived files describe the historical tree and may no longer resolve. Do not repair them by linking old code back into the runtime. Port any useful algorithm into the module that owns that job now, and test it there.

## Retired in Phases 22 and 23 (2026-09-25)

Phase 22 removed OpenGL and the editor. Phase 23 replaced the transitional renderer with the modern runtime. See [docs/EngineRuntime.md](../docs/EngineRuntime.md).

| Archived | Replaced by / status |
| --- | --- |
| `Engine/Systems/Renderer/OpenGL/*` | Removed with the OpenGL backend (Phase 22). Vulkan through the RHI is the only renderer; the configuration rejects `--graphics=opengl`. |
| `Engine/Systems/Renderer/Vulkan/*` (`VulkanRenderer`, index/instanced draw, buffers, pipelines, GPU BVH) | `Source/Engine/Systems/Renderer/Runtime/` (`RenderDevice`, `FrameRenderer`) on the RHI, RenderGraph, GPU scene, GPU visibility and clustered Forward+. |
| `Engine/Systems/Renderer/Renderer.*`, `Core/RendererRuntimeServices.h` | `RenderServices` (`Renderer/Runtime/RenderServices.h`) and the explicitly owned runtime in `SwimEngine`. |
| `Engine/Systems/Renderer/Core/Meshes/*` (`MeshPool`, `Mesh`, `PrimitiveMeshes`, `Vertex`) | `MeshLibrary` and `ProceduralMeshes` streaming through `AssetResidencyService` into the `GeometryHeap`. |
| `Engine/Systems/Renderer/Core/Material/*` (`MaterialPool`, `MaterialData`, `LegacyRenderBinding`) | `MaterialLibrary`, which builds the GPU material table from `MaterialDesc`. |
| `Engine/Systems/Renderer/Core/Textures/*` (`TexturePool`, `Texture2D`) and `Engine/ThirdParty/Stb*Implementation.cpp` | `TextureResidency` and the bindless table. `MeshLibrary::RegisterTexture` handles procedural textures; cooked textures arrive through `.sasset`. |
| `Engine/Systems/Renderer/Core/Font/*`, `Core/Ui/*`, `Engine/Components/TextComponent.h`, `MeshDecorator.h` | `Systems/Text`, `Systems/UI` and `Renderer/UiRendering`, driven by `UiRuntime` and the `UiCanvas` component. |
| `Engine/Systems/Renderer/Core/Environment/*` (`CubeMapController`, skybox) | The procedural sky background pass and the GPU-built environment (prefiltered cube, irradiance, BRDF LUT). |
| `Engine/Systems/Renderer/Core/Camera/*` (legacy `CameraSystem`, `Frustum`) and `Core/RenderConventions.h` | `Source/Engine/Systems/Camera/` (right-handed, reverse-Z `Camera`) and GPU frustum culling in `Renderer/Visibility`. |
| `Engine/Systems/Renderer/Core/MathTypes/*` | GLM plus the row-major helpers in `Renderer/Visibility/RenderViewDesc.h`. |
| `Engine/Systems/Scene/SubSceneSystems/SceneBVH.*`, `Engine/Components/Internal/FrustumCullCache.h` | GPU visibility (frustum and LOD culling, binning, indirect commands). |
| `Engine/Systems/Scene/SubSceneSystems/GizmoSystem.*`, `SceneDebugDraw.*`, `Scene/InternalBehaviors/*` (gizmo buttons, editor camera control) | Editor removed (Phase 22). The fly camera is now the runtime behaviour `Engine::FlyCameraController`. |
| `Engine/Systems/Entity/CommonBehaviors/DragUiBehavior.*` | Retained UI controls and `UiCanvasRouter` input. |
| `Engine/Components/Material.h`, `CompositeMaterial.h` | `MeshRenderer` parts (mesh handle plus material id). |
| `Engine/Components/ObjectTag.h` | `Engine/Components/Tags.h`: several hashed tags per entity, a `TagRegistry` and the scene's tag index. |
| `Game/Scenes/SandBox.h`, `Game/Scenes/Sandbox.cpp` (the old sandbox) | The new `Game::Sandbox` demo scene. |
| `Game/Behaviors/Demo/*`, `Phys/*`, `Util/*` | `Game/Behaviors/Motion` (Spin, Bob, Orbit, Lifetime), `BallShooter` and `Projectile`, `TentacleAnimator`. |
| `Game/Testing/*` (primitive, physics, mesh-stress and text/UI test scenes) | The sandbox's playgrounds, plus the headless `Game.Sandbox` and `Engine.SceneRuntime` tests. |
| `Shaders/OpenGL/*`, `Shaders/Slang/OpenGL/*`, `Shaders/Slang/Vulkan/*`, `Shaders/Vulkan/*` | The runtime shader set under `Source/Shaders/Slang` (Forward+, shadows, post, UI, particles, sky, present, …). |
| `Tests/Suites/Scene/Ecs/FrustumTests.cpp`, `RenderConventionsTests.cpp` | Tests of the retired CPU frustum and conventions. The camera convention is covered by `Engine.Camera`. |
| `cmake/LegacyDependencies.cmake` | The retired third-party stack: GLAD/OpenGL, nlohmann/json, stb, zstd, the Basis transcoder and the legacy font helpers. `cmake/Dependencies.cmake` now fetches only EnTT and spdlog. |

## Retired earlier

| Archived | Replaced by / status |
| --- | --- |
| `Engine/Systems/IO/InputManager.*` | `Source/Engine/Input/InputSystem.*`, owned directly by `SwimEngine` and injected into scenes and behaviours. |
| `Engine/Systems/IO/CommandSystem.*` | `Source/Engine/Commands/CommandRegistry.*`, an in-process callback registry with no `Machine` lifecycle and no external transport. |
| `Engine/Systems/SystemManager.*` | Explicit typed ownership and lifecycle in `SwimEngine`. |
| `Engine/Platform/EditorIpcBridge.*` | The retired external-process editor transport. |
| `Engine/Systems/Scene/Serialization/Scene*` | The retired scene-JSON, storage and editor-sync experiment. Durable entity identity remains active under `Source/Engine/Systems/Scene/Identity/`. |
| `Engine/Systems/Scene/SceneSystemEditorCommands.*` | Fragments from the old `#if 0` editor-command blocks. They cannot be built on their own. |
| `Shaders/README.md` | Notes on the pre-Slang HLSL/GLSL sources. |
