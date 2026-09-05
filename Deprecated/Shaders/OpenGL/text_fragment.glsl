#version 460 core
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D msdfAtlas;

// Style
uniform vec4  fillColor;
uniform vec4  strokeColor;
uniform float strokeWidthPx;   // in screen pixels

// From JSON "atlas.distanceRange" (MSDF range in *atlas pixels*)
uniform float msdfPixelRange;

float median3(float a, float b, float c)
{
    // Median of three: robust way to recover the signed distance value from RGB
    return max(min(a, b), min(max(a, b), c));
}

// Returns how many *atlas texels* a single screen pixel spans at this UV.
// We clamp to >= 1 to avoid crazy divisions when derivatives get tiny.
float screenPxRange(vec2 uv)
{
    vec2 texSize = vec2(textureSize(msdfAtlas, 0));
    vec2 duv_dx  = dFdx(uv) * texSize;
    vec2 duv_dy  = dFdy(uv) * texSize;
    float scale  = max(length(duv_dx), length(duv_dy));
    return max(scale, 1.0);
}

void main()
{
    // Flip V for our atlas
    vec2 uv = vec2(vUV.x, 1.0 - vUV.y);

    // Sample MSDF (linear, clamp, no mips as per your setup)
    vec3 ms  = texture(msdfAtlas, uv).rgb;

    // Reconstruct signed distance in normalized MSDF units centered at 0
    float sd = median3(ms.r, ms.g, ms.b) - 0.5;

    // Convert to *atlas pixels* (this distance is clamped to ±0.5*msdfPixelRange by construction)
    float distPxAtlas = sd * msdfPixelRange;

    // How many atlas pixels per screen pixel at this location
    float spr = screenPxRange(uv);

    // ------------------------
    // FILL COVERAGE (scale invariant)
    // ------------------------
    // Classic MSDF antialiasing: convert to screen-pixel distance by dividing by 'spr'
    float aFill = clamp(distPxAtlas / spr + 0.5, 0.0, 1.0) * fillColor.a;

    // ------------------------
    // STROKE COVERAGE
    // ------------------------
    float aStroke = 0.0;
    if (strokeWidthPx > 0.0)
    {
        // Distance from the zero-contour in *screen pixels*
        float edgeDistPx = abs(distPxAtlas) / spr;

        // Desired stroke band centered at the zero iso-contour.
        // The +/-0.75 gives a ~1.5px soft edge in screen space.
        float strokeBand = 1.0 - smoothstep(strokeWidthPx - 0.75, strokeWidthPx + 0.75, edgeDistPx);

        // --------- IMPORTANT GUARD ----------
        // MSDF only encodes distances up to half-range (±0.5*msdfPixelRange).
        // Convert that *maximum encodable distance* into screen pixels and
        // fade the stroke to 0 beyond that support. This removes the "boxy quad" halo
        // that appears when the glyph gets very small.
        float maxEncodableDistPx_screen = (0.5 * msdfPixelRange) / spr;

        // Add a small transition width ~ 1 atlas pixel mapped into screen pixels.
        float supportEdge = maxEncodableDistPx_screen;
        float supportSoft = max(1.0 / spr, 1e-4); // avoid zero-width smoothstep

        // supportMask = 1 inside the valid MSDF support band, ~0 at/after saturation.
        // Using 1 - smoothstep(supportEdge - supportSoft, supportEdge, edgeDistPx)
        // gives a soft falloff instead of a hard cut.
        float supportMask = 1.0 - smoothstep(supportEdge - supportSoft, supportEdge, edgeDistPx);

        aStroke = strokeBand * supportMask * strokeColor.a;
    }

    // Combined premultiplied coverage
    float a = clamp(aFill + aStroke, 0.0, 1.0);

    // Cull nearly transparent fragments to keep overdraw clean
    if (a < 0.002)
    {
        discard;
    }

    // PREMULTIPLIED output
    vec3 rgb = fillColor.rgb * aFill + strokeColor.rgb * aStroke;
    FragColor = vec4(rgb, a);
}
