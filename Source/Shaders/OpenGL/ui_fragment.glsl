#version 460 core

in vec2 fragUV;
out vec4 FragColor;

uniform vec4 fillColor;
uniform vec4 strokeColor;
uniform vec2 strokeWidth;
uniform vec2 cornerRadius;
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

  float dist = roundCorners != 0
    ? sdRoundRect(p, halfSize, cornerRadius)
    : max(abs(p.x) - halfSize.x, abs(p.y) - halfSize.y);

  float aa = fwidth(dist);

  vec4 result = vec4(0.0);

  if (enableFill != 0)
  {
    vec2 fillHalfSize = halfSize - strokeWidth;
    vec2 fillRadius = max(cornerRadius - strokeWidth, vec2(0.0));
    float fillDist = sdRoundRect(p, fillHalfSize, fillRadius);

    float fillAlpha = 1.0 - smoothstep(-aa, aa, fillDist);
    result = fillColor * fillAlpha;
  }

  if (enableStroke != 0)
  {
    float outerAlpha = 1.0 - smoothstep(-aa, aa, dist);

    vec2 innerHalfSize = halfSize - strokeWidth;
    vec2 innerRadius = max(cornerRadius - strokeWidth, vec2(0.0));
    float innerDist = sdRoundRect(p, innerHalfSize, innerRadius);

    float innerAlpha = 1.0 - smoothstep(-aa, aa, innerDist);
    float strokeAlpha = outerAlpha - innerAlpha;

    result = mix(result, strokeColor, strokeAlpha * strokeColor.a);
  }

  if (result.a <= 0.001)
  {
    discard;
  }

  FragColor = result;
}