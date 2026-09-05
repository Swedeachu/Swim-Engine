// ====== MSDF Text PS (World + Screen Space Compatible) ======
[[vk::binding(0, 0)]]
cbuffer CameraUBO : register(b0, space0)
{
    float4x4 worldView;
    float4x4 worldProj;
    float4x4 screenView;
    float4x4 screenProj;

    float4   camParams;
    float2   viewportSize;
    float2   _padViewportSize;
};

struct MsdfTextGpuInstanceData
{
    column_major float4x4 modelTR;
    float4   plane;          // (l,b,r,t) in EM
    float4   uvRect;         // (uL,vB,uR,vT)  <-- IMPORTANT
    float4   fillColor;
    float4   strokeColor;
    float    strokeWidthPx;
    float    msdfPixelRange; // atlas pixel range
    float    emScalePx;
    int      space;          // 0=world, 1=screen
    float2   pxToModel;
    uint     atlasTexIndex;
    uint     _pad_;
};

[[vk::binding(3, 0)]]
StructuredBuffer<MsdfTextGpuInstanceData> gMsdfParams : register(t3, space0);

[[vk::binding(0, 1)]] SamplerState gBindlessSampler : register(s0, space1);
[[vk::binding(1, 1)]] Texture2D    gBindlessTex[]   : register(t1, space1);

struct PSIn
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;       // unit quad (0..1)
    uint   iid : SV_InstanceID;
};

float median3(float a, float b, float c) { return max(min(a, b), min(max(a, b), c)); }

float4 main(PSIn IN) : SV_Target
{
    MsdfTextGpuInstanceData P = gMsdfParams[IN.iid];

    // --- Map unit UV -> glyph's sub-rect in the atlas ---
    // P.uvRect = (uL, vB, uR, vT) from your CPU path
    float2 uvMin = P.uvRect.xy;
    float2 uvMax = P.uvRect.zw;
    float2 uv    = lerp(uvMin, uvMax, IN.uv);

    // Vulkan/HLSL convention: flip V if your atlas data was authored GL-style (origin bottom-left)
    uv.y = 1.0 - uv.y;

    // Bindless atlas
    uint idx = NonUniformResourceIndex(P.atlasTexIndex);
    Texture2D atlas = gBindlessTex[idx];

    // Texture size (for pixel-range calc)
    uint w, h;
    atlas.GetDimensions(w, h);
    float2 texSize = float2(w, h);

    // Derivatives in texture space
    float2 uv_dx = ddx_fine(uv);
    float2 uv_dy = ddy_fine(uv);

    // If sampler has no mips (recommended for MSDF), lock to LOD 0.
    // Otherwise SampleGrad is fine too. SampleLevel avoids any accidental LOD blur.
    float3 ms = atlas.SampleLevel(gBindlessSampler, uv, 0.0).rgb;
    // float3 ms = atlas.SampleGrad(gBindlessSampler, uv, uv_dx, uv_dy).rgb;

    // Reconstruct signed distance (in normalized MSDF units)
    float sd = median3(ms.r, ms.g, ms.b) - 0.5;

    // Convert to atlas pixels
    float distPxAtlas = sd * P.msdfPixelRange;

    // Screen-pixel coverage factor: how many atlas texels a screen pixel spans
    float2 duv_dx = uv_dx * texSize;
    float2 duv_dy = uv_dy * texSize;
    float  spr    = max( max(length(duv_dx), length(duv_dy)), 1.0 );

    // Fill coverage (scale-invariant MSDF)
    float aFill = saturate(distPxAtlas / spr + 0.5) * P.fillColor.a;

    // Stroke coverage
    float aStroke = 0.0;
    if (P.strokeWidthPx > 0.0)
    {
        float edgeDistPx = abs(distPxAtlas) / spr;

        float strokeBand = 1.0 - smoothstep(
            P.strokeWidthPx - 0.75,
            P.strokeWidthPx + 0.75,
            edgeDistPx
        );

        float maxEncodableDistPx_screen = (0.5 * P.msdfPixelRange) / spr;
        float supportEdge = maxEncodableDistPx_screen;
        float supportSoft = max(1.0 / spr, 1e-4);

        float supportMask = 1.0 - smoothstep(
            supportEdge - supportSoft,
            supportEdge,
            edgeDistPx
        );

        aStroke = strokeBand * supportMask * P.strokeColor.a;
    }

    float a = saturate(aFill + aStroke);
    if (a < 0.002) discard;

    float3 rgb = P.fillColor.rgb * aFill + P.strokeColor.rgb * aStroke;
    return float4(rgb, a); // premultiplied
}
