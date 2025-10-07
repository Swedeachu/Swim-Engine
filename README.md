# Swim Engine

This engine is built using **EnTT**, a custom **Scene System**, my own **Windows messaging framework**, and fully supports both **Vulkan** and **OpenGL** rendering backends.  
The project is my life's work, nearly all knowledge in engineering I have goes into this in one way or another.
<br>
<br>
<img width="1260" height="583" alt="image" src="https://github.com/user-attachments/assets/b4a0f02d-65f6-4f38-b40c-8fc865500420" />

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
