#version 460 core

in vec2 fragUV;
out vec4 FragColor;

uniform vec4 fillColor;
uniform vec4 strokeColor;
uniform vec2 strokeWidth;   // in UV units
uniform vec2 cornerRadius;  // in UV units
uniform int enableFill;
uniform int enableStroke;
uniform int roundCorners;

float sdRoundRect(vec2 p, vec2 halfSize, vec2 rad)
{
  vec2 q = abs(p) - halfSize + rad;
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - min(rad.x, rad.y);
}

void main()
{
  vec2 p = fragUV - 0.5;
  vec2 halfSize = vec2(0.5);

  // Stretch correction
  float aspect = halfSize.x / halfSize.y;
  p.x *= aspect;

  vec2 clampedRadius = min(cornerRadius, halfSize);
  float dist = roundCorners != 0
    ? sdRoundRect(p, halfSize, clampedRadius)
    : max(abs(p.x) - halfSize.x, abs(p.y) - halfSize.y);

  float aa = fwidth(dist);

  vec4 result = vec4(0.0);

  if (enableFill != 0)
  {
    float fillAlpha = 1.0 - smoothstep(0.0, aa, dist);
    result = fillColor * fillAlpha;
  }

  if (enableStroke != 0)
  {
    float outerDist = dist - max(strokeWidth.x, strokeWidth.y) * 0.5;
    float strokeAlpha = smoothstep(aa, 0.0, outerDist) * smoothstep(0.0, aa, dist);
    result = mix(result, strokeColor, strokeAlpha * strokeColor.a);
  }

  if (result.a <= 0.001)
  {
    discard;
  }

  FragColor = result;
}
