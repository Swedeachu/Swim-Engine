#version 460 core

layout(location = 0) in vec3 aPos;
out vec3 TexCoords;

uniform mat4 projection;
uniform mat4 view;
uniform mat3 rotation;  // 3x3 rotation matrix for rotating the skybox

void main()
{
  // Rotate the incoming direction vector
  TexCoords = rotation * aPos;

  // Project with no translation (preserved behavior)
  vec4 pos = projection * view * vec4(aPos, 1.0);
  gl_Position = pos.xyww;
}
