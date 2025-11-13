#version 460 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color; // vertex color input
layout(location = 2) in vec2 uv;

noperspective out vec2 fragUV;  // avoid perspective distortion for screen-space SDF
out vec3 fragColor;             // pass-through color to fragment shader

uniform mat4 mvp;
uniform int renderOnTop; // 0 = normal depth, positive values = force layer in front

void main()
{
  fragUV = uv;
  fragColor = color;
  vec4 clipPos = mvp * vec4(position, 1.0);
  
  // Handle layered renderOnTop for depth control
  if (renderOnTop > 0) {
    // Map renderOnTop layer to depth in reverse order (higher layer = closer to camera)
    // Layer 0 = normal depth testing (no modification)
    // Layer 1+ = progressively closer to near plane
    
    // Maximum supported layers 
    const int maxLayers = 100;
    
    // Clamp layer value
    int layer = min(renderOnTop, maxLayers);
    
    // Calculate depth step size
    // We'll use the range [-1.0 + epsilon, -0.8] for all forced layers
    // Higher layer number = larger z value (closer to camera in OpenGL NDC)
    const float nearEpsilon = 1e-6;
    const float nearForced = -1.0 + nearEpsilon;  // closest to near plane
    const float farForced = -0.8;                  // farthest in forced range
    const float depthRange = farForced - nearForced;
    
    // Linear interpolation: layer 1 is farthest in forced range, layer maxLayers is nearest
    float t = float(maxLayers - layer) / float(maxLayers);
    float forcedDepth = nearForced + t * depthRange;
    
    // Set z to forced depth value (multiply by w for clip space)
    // OpenGL NDC: near plane = -w, far plane = +w
    clipPos.z = clipPos.w * forcedDepth;
  }
  
  gl_Position = clipPos;
}