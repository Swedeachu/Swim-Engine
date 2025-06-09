#version 460 core

in vec2 fragUV;
out vec4 FragColor;

uniform vec4 fillColor;
uniform vec4 strokeColor;
uniform vec2 strokeWidth;
uniform vec2 cornerRadius;

uniform int enableFill;   // if 1, draw with fill color, useTexture will override this if the material texture is set
uniform int enableStroke; // if 1, use stroke width and color
uniform int roundCorners; // if 1, use rounded corners based on corner radius 
uniform int useTexture;   // if 1, sample texture instead of using solid fillColor

uniform vec2 resolution;
uniform vec2 quadSize;

uniform sampler2D albedoTex; // texture to sample if useTexture is enabled

// Signed distance function for a rounded rectangle
float roundedRectSDF(vec2 pos, vec2 size, float radius)
{
  vec2 d = abs(pos) - size + radius;
  return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - radius;
}

// Signed distance for a box (non-rounded)
float boxSDF(vec2 pos, vec2 size)
{
  vec2 d = abs(pos) - size;
  return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

void main()
{
  // Convert UV (0-1) to centered space (-0.5 to 0.5)
  vec2 uv = fragUV - 0.5;

  // Transform into pixel space
  vec2 pos = uv * quadSize;

  // Compute half-extents of the quad
  vec2 halfSize = quadSize * 0.5;

  // Determine corner radius from uniform if roundCorners enabled
  float radius = (roundCorners == 1) ? max(cornerRadius.x, cornerRadius.y) : 0.0;

  // Clamp radius to prevent exceeding half-dimensions
  float maxRadius = min(halfSize.x, halfSize.y);
  radius = min(radius, maxRadius);

  // Inner half-size is used for the fill region inside the stroke
  vec2 innerHalfSize = max(halfSize - strokeWidth, vec2(0.0));

  // Inner radius for fill area must also be clamped
  float innerRadius = max(radius - max(strokeWidth.x, strokeWidth.y), 0.0);

  // Calculate SDF distance to outer (stroke or fill edge)
  float distOuter = (radius > 0.0) ? roundedRectSDF(pos, halfSize, radius)
    : boxSDF(pos, halfSize);

  // Calculate SDF distance to inner (fill) region
  float distInner = (radius > 0.0) ? roundedRectSDF(pos, innerHalfSize, innerRadius)
    : boxSDF(pos, innerHalfSize);

  // Antialiased edge width (in pixels)
  float aaWidth = 1.0;

  // Outer boundary smooth fade (stroke or fill)
  float outerAlpha = 1.0 - smoothstep(-aaWidth, aaWidth, distOuter);

  // Inner boundary smooth fade (fill interior)
  float innerAlpha = 1.0 - smoothstep(-aaWidth, aaWidth, distInner);

  // Stroke alpha is the ring between outer and inner regions
  float strokeAlpha = (enableStroke == 1) ? clamp(outerAlpha - innerAlpha, 0.0, 1.0) : 0.0;

  // Fill alpha comes from the inner region if enabled
  float fillAlpha = (enableFill == 1) ? innerAlpha : 0.0;

  // Sample from texture if texture is enabled
  vec4 texSample = texture(albedoTex, fragUV);

  // Use texture color or fallback to fillColor
  vec4 fillSrc = (useTexture == 1) ? texSample : fillColor;

  // Mix fill and stroke colors by stroke alpha
  vec3 finalColor = mix(fillSrc.rgb, strokeColor.rgb, strokeAlpha);

  // Mix alpha between fill and stroke contributions
  float finalAlpha = mix(fillSrc.a * fillAlpha, strokeColor.a, strokeAlpha);

  // Final pixel output
  FragColor = vec4(finalColor, finalAlpha);
}
