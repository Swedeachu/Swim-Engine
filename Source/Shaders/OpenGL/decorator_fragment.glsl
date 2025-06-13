#version 460 core

in vec2 fragUV;
in vec3 fragColor;

out vec4 FragColor;

uniform vec4 fillColor;
uniform vec4 strokeColor;
uniform vec2 strokeWidth;
uniform vec2 cornerRadius;

uniform int enableFill;
uniform int enableStroke;
uniform int roundCorners;
uniform int useTexture;

uniform vec2 resolution;
uniform vec2 quadSize;

uniform sampler2D albedoTex;

uniform int isWorldSpace; // 1 = world, 0 = screen

float roundedRectSDF(vec2 pos, vec2 size, float radius)
{
  vec2 d = abs(pos) - size + radius;
  return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - radius;
}

float boxSDF(vec2 pos, vec2 size)
{
  vec2 d = abs(pos) - size;
  return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

void main()
{
  vec2 uv = fragUV - 0.5;
  vec2 pos = uv * quadSize;

  vec2 halfSize = quadSize * 0.5;

  float radius = (roundCorners == 1) ? max(cornerRadius.x, cornerRadius.y) : 0.0;
  radius = min(radius, min(halfSize.x, halfSize.y));

  vec2 innerHalfSize = max(halfSize - strokeWidth, vec2(0.0));
  float innerRadius = max(radius - max(strokeWidth.x, strokeWidth.y), 0.0);

  float distOuter = (radius > 0.0) ? roundedRectSDF(pos, halfSize, radius) : boxSDF(pos, halfSize);
  float distInner = (radius > 0.0) ? roundedRectSDF(pos, innerHalfSize, innerRadius) : boxSDF(pos, innerHalfSize);

  float aaWidth = 1.0;
  float outerAlpha = 1.0 - smoothstep(-aaWidth, aaWidth, distOuter);
  float innerAlpha = 1.0 - smoothstep(-aaWidth, aaWidth, distInner);

  float strokeAlpha = (enableStroke == 1) ? clamp(outerAlpha - innerAlpha, 0.0, 1.0) : 0.0;
  float fillAlpha = (enableFill == 1) ? innerAlpha : 0.0;

  vec4 texSample = texture(albedoTex, fragUV);

  bool useVertexColor = all(lessThan(fillColor.rgb, vec3(0.0)));
  vec4 fillSrc = vec4(1.0);

  if (useTexture == 1)
  {
    fillSrc = texSample;
  }
  else if (useVertexColor)
  {
    fillSrc = vec4(fragColor, 1.0);
  }
  else
  {
    fillSrc = fillColor;
  }

  float combinedAlpha = clamp(fillAlpha + strokeAlpha, 0.0, 1.0);
  vec3 combinedColor = vec3(0.0);
  if (combinedAlpha > 0.0)
  {
    combinedColor = (fillSrc.rgb * fillAlpha + strokeColor.rgb * strokeAlpha) / combinedAlpha;
  }

  FragColor = vec4(combinedColor, combinedAlpha);
}
