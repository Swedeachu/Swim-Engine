# This readme won't suck once this main todo list is done
Using EnTT, my own Scene System and engine + windows messaging framework, and Vulkan

# Current features
Scene system powered by EntT ECS + Behavior components system
<br>
GLTF/GLB loading + texture loading (bindless + mip support + various image formats)
<br>
Vulkan and OpenGL renderer engine abstraction contexts with full visual feature parity
<br>
Cubemap skybox rendering with parameter controls for rotation and side textures
<br>
Scene BVH spacial partition for traversal of quick frustum culling and ray casts
<br>
Vulkan GPU driven bindless indexed indirect rendering 
<br>
MSDF text + stylized screen and world SDF rendering (outline stroke, colors, roundness)
<br>
Debug immediate mode 3D rendering of primitive meshes
<br>
Keyboard + mouse input via input manager system hooked to windows messages

# CURRENT TODO
Scene editor gizmos and property value editors for primitive component fields
<br>
Archetypes and prefabs for Behavior components and full scene serialization
<br>
Hook up PhysX/Jolt
<br>
Compute culling
<br>
Recursive propagating parent child hierarchy for UI entities
<br>
Controller support in input manager
<br>
DLL and C# bindings + run time for editors and other projects

# SUPER TODO ONCE CURRENT TODO IS DONE
PBR 
<br>
Refactor to Clustered Forward+ for global illumination
<br>
Shadows (baked and dynamic)
<br>
GPU driven particle system
<br>
Skeletal animation system and rag dolls/boned physics bound meshes
<br>
MiniAudio integration
<br>
Thread workers for FileIO and async scene loading via asset streaming
<br>
Binary GPU buffer formats for all assets