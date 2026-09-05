# Retired code

Nothing under this directory is compiled, linked, packaged as runtime code, or included by active `Source/` files. These files preserve historical implementations for reference. They are not compatibility modules or a second route to current services.

| Archived implementation | Active replacement / status |
| --- | --- |
| `Engine/Systems/IO/InputManager.*` | `Source/Engine/Input/InputSystem.*`, directly owned by `SwimEngine` and injected into scenes/behaviors. |
| `Engine/Systems/IO/CommandSystem.*` | `Source/Engine/Commands/CommandRegistry.*`, an in-process callback registry with no `Machine` lifecycle or external transport. |
| `Engine/Systems/SystemManager.*` | Explicit typed ownership and lifecycle in `SwimEngine`. No consumers remained. |
| `Engine/Platform/EditorIpcBridge.*` | Retired external-process editor transport. Future editor tools are in-process; no replacement transport is currently needed. |
| `Engine/Systems/Scene/Serialization/Scene*` | Retired scene-JSON/storage/editor-sync experiment. Durable runtime entity identity remains active under `Source/Engine/Systems/Scene/Identity/`. Future persistence must be implemented against current contracts. |
| `Engine/Systems/Scene/SceneSystemEditorCommands.*` | Historical declaration/definition fragments extracted from the old `#if 0` editor-command blocks. They are deliberately not buildable standalone modules. |

Archived imports and APIs describe the historical tree and may no longer resolve. Do not repair them by linking old code back into the runtime. Port useful algorithms into the current owning module and test them there.

The old Vulkan/OpenGL renderers and renderer-facing asset pools still have live consumers and remain in `Source/`. Their replacement and retirement gates are tracked in `docs/SwimEngineArchitectureImplementationPlan.md`, section 0.3. They may move here only after those consumers switch to the modern renderer/residency contracts.

