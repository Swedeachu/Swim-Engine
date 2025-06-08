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
uniform vec2 resolution;
uniform vec2 quadSize;

const int perceptualAA = 1;
const vec2 ssOffsets[8] = vec2[](
  vec2(-0.375, -0.125),
  vec2(-0.125, -0.375),
  vec2(0.125, -0.375),
  vec2(0.375, -0.125),
  vec2(0.375, 0.125),
  vec2(0.125, 0.375),
  vec2(-0.125, 0.375),
  vec2(-0.375, 0.125)
  );

float sdRoundRectImproved(vec2 p, vec2 halfSize, vec2 rad)
{
  rad = min(rad, min(halfSize.x, halfSize.y));
  vec2 q = abs(p) - halfSize + rad;
  return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - rad.x;
}
float aaSmooth(float dist, float aa, int perceptual)
{
  float alpha = clamp(0.5 - dist / aa, 0.0, 1.0);
  if (perceptual != 0)
  {
    alpha = pow(alpha, 0.7);
  }
  return alpha;
}
void main()
{
  vec4 finalColor = vec4(0.0);
  vec2 pixelSizeInUV = 1.0 / quadSize;
  // Better anti-aliasing width calculation
  // Use the maximum component to handle non-square quads properly
  float aaWidth = max(pixelSizeInUV.x, pixelSizeInUV.y) * 0.5;

  // Determine effective stroke width (zero when stroke is disabled)
  vec2 effectiveStrokeWidth = (enableStroke != 0) ? strokeWidth : vec2(0.0);

  for (int i = 0; i < 8; ++i)
  {
    vec2 offset = ssOffsets[i] * pixelSizeInUV;
    vec2 sampleUV = fragUV + offset;
    vec2 p = sampleUV - 0.5;
    vec2 halfSize = vec2(0.5);
    vec4 sampleColor = vec4(0.0);

    if (enableStroke != 0)
    {
      // Calculate outer edge distance (shape boundary)
      float outerDist = (roundCorners != 0)
        ? sdRoundRectImproved(p, halfSize, cornerRadius)
        : max(abs(p.x) - halfSize.x, abs(p.y) - halfSize.y);
      // Calculate inner edge distance (stroke inner boundary)
      vec2 innerHalfSize = halfSize - strokeWidth;
      vec2 innerRadius = max(cornerRadius - strokeWidth, vec2(0.0));
      float innerDist = (roundCorners != 0)
        ? sdRoundRectImproved(p, innerHalfSize, innerRadius)
        : max(abs(p.x) - innerHalfSize.x, abs(p.y) - innerHalfSize.y);
      // Properly blend both edges
      float outerAlpha = aaSmooth(outerDist, aaWidth, perceptualAA);
      float innerAlpha = 1.0 - aaSmooth(innerDist, aaWidth, perceptualAA);
      // Stroke alpha is the intersection of being inside outer edge and outside inner edge
      float strokeAlpha = outerAlpha * innerAlpha;
      sampleColor = strokeColor * strokeAlpha;
    }

    if (enableFill != 0)
    {
      // Use effective stroke width (zero when stroke disabled)
      vec2 fillHalfSize = halfSize - effectiveStrokeWidth;
      vec2 fillRadius = max(cornerRadius - effectiveStrokeWidth, vec2(0.0));

      float fillDist = (roundCorners != 0)
        ? sdRoundRectImproved(p, fillHalfSize, fillRadius)
        : max(abs(p.x) - fillHalfSize.x, abs(p.y) - fillHalfSize.y);

      float fillAlpha = aaSmooth(fillDist, aaWidth, perceptualAA);
      // Blend fill over stroke
      sampleColor = mix(sampleColor, fillColor, fillAlpha * fillColor.a);
    }

    finalColor += sampleColor;
  }

  finalColor /= 8.0;

  if (finalColor.a < 0.000001)
  {
    discard;
  }

  FragColor = finalColor;
}