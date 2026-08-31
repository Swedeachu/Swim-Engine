# Swim Engine

This engine is built using **EnTT**, a custom **Scene System**, my own **Windows messaging framework**, and fully supports both **Vulkan** and **OpenGL** rendering backends.  
The project is my life's work, nearly all knowledge in engineering I have goes into this in one way or another.
<br>
<br>
<img width="1260" height="583" alt="image" src="https://github.com/user-attachments/assets/b4a0f02d-65f6-4f38-b40c-8fc865500420" />

---

## Building

CMake is the only build-system source of truth. The repository does not commit generated Visual Studio projects or third-party source/binary trees. Dependencies are pinned and resolved into `build/_deps` with CPM.cmake on first configure.

### Prerequisites

- CMake 3.25 or newer.
- Visual Studio 2022 with the Desktop development with C++ workload.
- Git and Python 3. Python is used by GLAD and by PhysX's upstream project bootstrap.
- Ninja for the terminal presets.
- A Vulkan SDK that provides Vulkan headers/libraries and `dxc.exe` for the existing HLSL-to-SPIR-V shader build.

The current `compute-v2` code still contains Win32 platform/input/window APIs, so this CMake migration intentionally preserves Windows x64 as the supported runtime build. Linux support belongs to the platform refactor rather than being faked by build files alone.

### Visual Studio solution

Generate a normal Visual Studio 2022 solution with:

```powershell
cmake --preset windows-vs
```

Then open `build/windows-vs/SwimEngine.sln`. The solution is generated from the same `CMakeLists.txt` used by every other workflow. Solution Explorer mirrors the physical `Source/...` tree, and adding/removing/renaming C/C++ or shader files under `Source/Engine`, `Source/Game`, or `Source/Shaders` is picked up automatically on the next build through `GLOB_RECURSE ... CONFIGURE_DEPENDS`.

Build either configuration from the terminal with:

```powershell
cmake --build --preset windows-vs
# or, for the legacy Debug + PhysX Checked configuration:
cmake --build --preset windows-vs-debug
```

The generated executable keeps the historical name `Swim Engine.exe`.

### Ninja + MSVC

```powershell
scripts\build-windows.ps1 -Debug
scripts\build-windows.ps1
```

These use the `windows-debug` and `windows-release` presets respectively.

### Dependency policy

The previous `Source/Library` copies are replaced by pinned CMake targets for GLM, EnTT, nlohmann/json, stb, tinygltf, Draco, libwebp, zstd, Basis Universal, GLAD, and PhysX. Nothing downloaded by CMake should be committed.

PhysX is kept deliberately isolated because its configuration model does not match the application's Debug/Release model. The pinned PhysX 5.6.1 source is bootstrapped as a separate build under `build/_deps`; Swim Engine Debug links the **Checked** static PhysX libraries, while Swim Engine Release links **Release** PhysX. Both are built with the static non-debug MSVC runtime, matching the previous x64 project configuration (`/MT`, `PX_PHYSX_STATIC_LIB`, Debug `_ITERATOR_DEBUG_LEVEL=0`). The CPU-only VS2022 preset is used, so CUDA is not required.

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
- **Input System:** Keyboard and mouse input handled through Windows message hooks.

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
