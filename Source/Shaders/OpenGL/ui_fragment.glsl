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

    // Compute half-extents
    vec2 halfSize = quadSize * 0.5;

    // Determine corner radius
    float radius = (roundCorners == 1) ? max(cornerRadius.x, cornerRadius.y) : 0.0;
    float maxRadius = min(halfSize.x, halfSize.y);
    radius = min(radius, maxRadius);

    // Calculate stroke-related bounds
    vec2 innerHalfSize = halfSize - strokeWidth;
    innerHalfSize = max(innerHalfSize, vec2(0.0));
    float innerRadius = max(radius - max(strokeWidth.x, strokeWidth.y), 0.0);

    // Distance to outer edge
    float distOuter = (radius > 0.0) ? roundedRectSDF(pos, halfSize, radius) : boxSDF(pos, halfSize);

    // Distance to inner edge
    float distInner = (radius > 0.0) ? roundedRectSDF(pos, innerHalfSize, innerRadius) : boxSDF(pos, innerHalfSize);

    // Antialiased blend thresholds
    float aaWidth = 1.0;

    // Smooth alpha for outer and inner edges
    float outerAlpha = 1.0 - smoothstep(-aaWidth, aaWidth, distOuter);
    float innerAlpha = 1.0 - smoothstep(-aaWidth, aaWidth, distInner);

    // Compute stroke and fill alphas with toggles
    float strokeAlpha = (enableStroke == 1) ? clamp(outerAlpha - innerAlpha, 0.0, 1.0) : 0.0;
    float fillAlpha = (enableFill == 1) ? innerAlpha : 0.0;

    // Combine fill and stroke colors based on alpha weights
    vec3 finalColor = mix(fillColor.rgb, strokeColor.rgb, strokeAlpha);
    float finalAlpha = mix(fillColor.a * fillAlpha, strokeColor.a, strokeAlpha);

    FragColor = vec4(finalColor, finalAlpha);
}
