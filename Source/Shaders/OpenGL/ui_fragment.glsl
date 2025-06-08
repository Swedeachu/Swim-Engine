#version 460 core

in vec2 fragUV;
out vec4 FragColor;

// === Uniforms ===
uniform vec4 fillColor;
uniform vec4 strokeColor;
uniform vec2 strokeWidth;
uniform vec2 cornerRadius;
uniform int enableFill;
uniform int enableStroke;
uniform int roundCorners;
uniform vec2 resolution; // Screen resolution (e.g., 800x600)

// === Constants ===
const int perceptualAA = 1; // Always enable perceptual smoothing

// Supersample grid offsets (4x SSAA in pixel units)
const vec2 ssOffsets[4] = vec2[](
  vec2(-0.25, -0.25),
  vec2(0.25, -0.25),
  vec2(-0.25, 0.25),
  vec2(0.25, 0.25)
  );

// === Distance function: Signed distance to a rounded rectangle ===
float sdRoundRect(vec2 p, vec2 halfSize, vec2 rad)
{
  vec2 q = abs(p) - halfSize + rad;
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - min(rad.x, rad.y);
}

// === Smooth AA falloff with optional perceptual correction ===
float aaSmooth(float dist, float aa, int perceptual)
{
  float alpha = 1.0 - smoothstep(-aa, aa, dist);
  if (perceptual != 0)
  {
    // Approximate perceptual smoothing (gamma ~2.2)
    alpha = pow(alpha, 0.7);
  }
  return alpha;
}

// === Main Fragment Shader Entry Point ===
void main()
{
  vec4 finalColor = vec4(0.0);

  // Loop over 4 sub-pixel samples (manual 4x SSAA)
  for (int i = 0; i < 4; ++i)
  {
    // Convert offset to UV space
    vec2 offset = ssOffsets[i] / resolution;
    vec2 sampleUV = fragUV + offset;
    vec2 p = sampleUV - 0.5; // Center at (0,0)
    vec2 halfSize = vec2(0.5);

    // Outer border distance
    float outerDist = (roundCorners != 0)
      ? sdRoundRect(p, halfSize, cornerRadius)
      : max(abs(p.x) - halfSize.x, abs(p.y) - halfSize.y);

    float aa = fwidth(outerDist) * 1.2; // Expand AA range slightly

    vec4 sampleColor = vec4(0.0);

    // === Fill pass ===
    if (enableFill != 0)
    {
      vec2 fillHalfSize = halfSize - strokeWidth;
      vec2 fillRadius = max(cornerRadius - strokeWidth, vec2(0.0));
      float fillDist = (roundCorners != 0)
        ? sdRoundRect(p, fillHalfSize, fillRadius)
        : max(abs(p.x) - fillHalfSize.x, abs(p.y) - fillHalfSize.y);

      float fillAlpha = aaSmooth(fillDist, aa, perceptualAA);
      sampleColor = fillColor * fillAlpha;
    }

    // === Stroke pass ===
    if (enableStroke != 0)
    {
      float outerAlpha = aaSmooth(outerDist, aa, perceptualAA);

      vec2 innerHalfSize = halfSize - strokeWidth;
      vec2 innerRadius = max(cornerRadius - strokeWidth, vec2(0.0));
      float innerDist = (roundCorners != 0)
        ? sdRoundRect(p, innerHalfSize, innerRadius)
        : max(abs(p.x) - innerHalfSize.x, abs(p.y) - innerHalfSize.y);

      float innerAlpha = aaSmooth(innerDist, aa, perceptualAA);
      float strokeAlpha = clamp(outerAlpha - innerAlpha, 0.0, 1.0);

      sampleColor = mix(sampleColor, strokeColor, strokeAlpha * strokeColor.a);
    }

    finalColor += sampleColor;
  }

  // Average the 4 supersampled results
  finalColor /= 4.0;

  // Discard transparent fragments to avoid fringe artifacts
  if (finalColor.a < 0.001)
  {
    discard;
  }

  FragColor = finalColor;
}
