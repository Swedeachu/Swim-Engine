#version 460 core

noperspective in vec2 fragUV;  // keep screen-space interpolation
in vec3 fragColor;

out vec4 FragColor;

uniform vec4 fillColor;
uniform vec4 strokeColor;
uniform vec2 strokeWidth;   // in pixels
uniform vec2 cornerRadius;  // in pixels

uniform int enableFill;
uniform int enableStroke;
uniform int roundCorners;
uniform int useTexture;

uniform vec2 resolution;    
uniform vec2 quadSize;      // in pixels (width, height)

uniform sampler2D albedoTex;

uniform int isWorldSpace; // (unused here but kept)

float roundedRectSDF(vec2 pos, vec2 halfSize, float radius)
{
  // pos, halfSize in pixels; radius in pixels
  vec2 d = abs(pos) - halfSize + radius;
  return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - radius;
}

float boxSDF(vec2 pos, vec2 halfSize)
{
  vec2 d = abs(pos) - halfSize;
  return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

void main()
{
  // Map UV [0,1] to pixel-centered coordinates on the quad:
  // pos in pixels, origin at quad center.
  vec2 halfSize = quadSize * 0.5;
  vec2 posPx    = (fragUV - 0.5) * quadSize;

  // Radius: match Vulkan (min(max(cornerRadius.x, cornerRadius.y), min(halfSize)))
  float radius = (roundCorners == 1)
               ? min(max(cornerRadius.x, cornerRadius.y), min(halfSize.x, halfSize.y))
               : 0.0;

  // Stroke inner geometry in pixels (same semantics as your Vulkan shader)
  vec2  innerHalf = halfSize;
  float innerRad  = radius;
  if (enableStroke == 1) {
    innerHalf = max(halfSize - strokeWidth, vec2(0.0));
    innerRad  = max(radius - max(strokeWidth.x, strokeWidth.y), 0.0);
  }

  // Signed distances (pixels)
  float distOuter = (radius > 0.0)
      ? roundedRectSDF(posPx, halfSize, radius)
      : boxSDF(posPx, halfSize);

  float distInner = (radius > 0.0)
      ? roundedRectSDF(posPx, innerHalf, innerRad)
      : boxSDF(posPx, innerHalf);

  // Anti-aliased edges derived from screen-space derivatives (matches Vulkan approach)
  const float edgeAA = 1.5;
  float aaWidthOuter = fwidth(distOuter) * edgeAA;
  float aaWidthInner = fwidth(distInner) * edgeAA;

  // Guard against degenerate derivatives on extremely small or minified geometry
  aaWidthOuter = max(aaWidthOuter, 1e-3);
  aaWidthInner = max(aaWidthInner, 1e-3);

  // Alpha ramps: “outside -> inside” using 0..aaWidth like in Vulkan
  float outerAlpha = 1.0 - smoothstep(0.0, aaWidthOuter, distOuter);
  float innerAlpha = 1.0 - smoothstep(0.0, aaWidthInner, distInner);

  // Match Vulkan’s small-alpha fringe kill to remove 1px dither/buzz
  if (outerAlpha < 0.2) outerAlpha = 0.0;
  if (innerAlpha < 0.2) innerAlpha = 0.0;

  // Compose fill/stroke
  float strokeAlpha = (enableStroke == 1) ? clamp(outerAlpha - innerAlpha, 0.0, 1.0) : 0.0;
  float fillAlpha   = (enableFill   == 1) ? innerAlpha : 0.0;

  // Source color selection (mirrors your Vulkan logic)
  vec4 texSample = texture(albedoTex, fragUV);
  bool useVertexColor = all(lessThan(fillColor.rgb, vec3(0.0)));
  vec4 fillSrc = vec4(1.0);

  if (useTexture == 1) {
    fillSrc = texSample;
  } else if (useVertexColor) {
    fillSrc = vec4(fragColor, 1.0);
  } else {
    fillSrc = fillColor;
  }

  float combinedAlpha = clamp(fillAlpha + strokeAlpha, 0.0, 1.0);
  vec3 combinedColor  = (combinedAlpha > 0.0)
      ? (fillSrc.rgb * fillAlpha + strokeColor.rgb * strokeAlpha) / combinedAlpha
      : vec3(0.0);

  FragColor = vec4(combinedColor, combinedAlpha);
}
