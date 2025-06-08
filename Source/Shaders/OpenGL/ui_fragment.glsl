#version 460 core

in  vec2 fragUV;     // assumed in [0,1] UV space
out vec4 FragColor;

/* Uniforms */
uniform vec4 fillColor;
uniform vec4 strokeColor;
uniform vec2 strokeWidth;   // in UV units (full width)
uniform vec2 cornerRadius;  // in UV units
uniform int  enableFill;
uniform int  enableStroke;
uniform int  roundCorners;

/* Signed distance function for rounded rectangle */
float sdRoundRect(vec2 p, vec2 halfSize, vec2 rad)
{
  vec2 q = abs(p) - halfSize + rad;
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - min(rad.x, rad.y);
}

void main()
{
  // Convert fragUV [0,1] to [-0.5, 0.5], centered around 0
  vec2 p = fragUV - 0.5;

  // Always (0.5, 0.5) for normalized quad
  vec2 halfSize = vec2(0.5);

  // Compute SDF distance
  float dist = roundCorners != 0
    ? sdRoundRect(p, halfSize, cornerRadius)
    : max(abs(p.x) - halfSize.x, abs(p.y) - halfSize.y);

  // Antialias width based on screen-space derivatives
  float aa = fwidth(dist);

  // FILL
  if (enableFill != 0)
  {
    float fillAlpha = 1.0 - smoothstep(0.0, aa, dist);
    FragColor = vec4(fillColor.rgb, fillColor.a * fillAlpha);
  }

  // STROKE
  if (enableStroke != 0)
  {
    float strokeOuter = dist - max(strokeWidth.x, strokeWidth.y);
    float strokeAlpha = smoothstep(aa, 0.0, strokeOuter) * smoothstep(0.0, aa, dist);
    FragColor = mix(FragColor, strokeColor, strokeAlpha * strokeColor.a);
  }

  // Fully discard transparent pixels
  if (FragColor.a <= 0.001)
  {
    discard;
  }
}
