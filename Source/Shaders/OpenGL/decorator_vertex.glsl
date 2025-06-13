#version 460 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color; // vertex color input
layout(location = 2) in vec2 uv;

out vec2 fragUV;
out vec3 fragColor; // pass-through color to fragment shader

uniform mat4 mvp;

void main()
{
  fragUV = uv;            // screen-space quad UV
  fragColor = color;      // forward vertex color to fragment shader
  gl_Position = mvp * vec4(position, 1.0);
}
