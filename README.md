# This readme won't suck once this main todo list is done
Using EnTT, my own Scene System and engine + windows messaging framework, and Vulkan
<br>
# TODO

Resize handling for camera and swap chain 
<br>
mesh pool | the mesh component is really just telling which mesh in the mesh pool to use for that entity with its transform
<br>
rotation for transforms and 3D primatives like spheres and cubes
<br>
texture rendering 
<br>
tinyobj loading 
<br>
refactor anything that isnt dynamic or a bad practice that would hurt performance in the long run 
<br>
set up the sandbox to do entity creation and spawning en mass to performance profile, then hook it up to PhysX or Jolt
<br>
behavior components with EnTT called in line with the scene system 
<br>
probably get imgui set up at some point
<br>
2D UI layer that has screen space transforms (always rendered on the angeled plane the camera faces)

# SUPER TODO ONCE MAIN TODO IS DONE
Ray tracer 
<br>
Baked lighting too
<br>
GPU driven particle system
<br>
PBR and normal maps 
<br>
high quality model meshes 
<br>
sky sphere/sky box environment tricks to make stuff look not chopped
<br>
Skeletal animation system and rag dolls/boned physics bound meshes
<br>
Multi thread certain aspects when needed? 
<br>
Make one single beatiful scene with some cool per object and full screen shader effects with physics bound scripted objects
