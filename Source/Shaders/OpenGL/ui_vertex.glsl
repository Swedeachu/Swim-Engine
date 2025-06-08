#version 460 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color; // unused
layout(location = 2) in vec2 uv;

out vec2 fragUV;

uniform mat4 mvp;

void main()
{
  fragUV = uv; // screen-space quad UV
  gl_Position = mvp * vec4(position, 1.0);
}
