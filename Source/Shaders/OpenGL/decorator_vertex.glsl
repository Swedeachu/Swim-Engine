#version 460 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color; // vertex color input
layout(location = 2) in vec2 uv;

noperspective out vec2 fragUV;  // avoid perspective distortion for screen-space SDF
out vec3 fragColor;             // pass-through color to fragment shader

uniform mat4 mvp;
uniform int renderOnTop; // 0 = normal depth, nonzero = force in front

void main()
{
  fragUV = uv;
  fragColor = color;

  vec4 clipPos = mvp * vec4(position, 1.0);

  if (renderOnTop != 0) {
    // OpenGL default NDC: near plane = -w
    clipPos.z = (-1.0 + 1e-6) * clipPos.w;
  }

  gl_Position = clipPos;
}
