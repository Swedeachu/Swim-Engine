#version 460 core

// Camera UBO: std140 layout, bound to binding point 0
layout(std140, binding = 0) uniform Camera
{
    mat4 view;
    mat4 proj;
};

// Per‐vertex inputs
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;
layout(location = 2) in vec2 uv;

// Outputs to fragment
out vec3 fragColor;
out vec2 fragUV;

uniform mat4 model;  // per‐object

void main()
{
    // pass through
    fragColor = color;
    fragUV    = uv;

    // Model -> View -> Projection
    gl_Position = proj * view * model * vec4(position, 1.0);
}
