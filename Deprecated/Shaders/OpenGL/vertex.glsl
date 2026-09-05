#version 460 core

// Per‐vertex inputs
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;
layout(location = 2) in vec2 uv;

// Outputs to fragment
out vec3 fragColor;
out vec2 fragUV;

// Single combined Model-View-Projection matrix (set per draw)
uniform mat4 mvp;

void main()
{
  fragColor = color;
  fragUV = uv;

  gl_Position = mvp * vec4(position, 1.0);
}
