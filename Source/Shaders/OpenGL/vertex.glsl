#version 460 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;
layout(location = 2) in vec2 uv;

uniform mat4 view;
uniform mat4 proj;
uniform mat4 model;

out vec3 fragColor;
out vec2 fragUV;

void main()
{
  fragColor = color;
  fragUV = uv;
  gl_Position = proj * view * model * vec4(position, 1.0);
}
