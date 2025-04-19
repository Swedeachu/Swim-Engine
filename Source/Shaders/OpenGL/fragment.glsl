#version 460 core

in vec3 fragColor;
in vec2 fragUV;

out vec4 FragColor;

uniform sampler2D albedoTex;
uniform float hasTexture;

void main()
{
  if (hasTexture > 0.5)
  {
    FragColor = texture(albedoTex, fragUV);
  }
  else
  {
    FragColor = vec4(fragColor, 1.0);
  }
}
