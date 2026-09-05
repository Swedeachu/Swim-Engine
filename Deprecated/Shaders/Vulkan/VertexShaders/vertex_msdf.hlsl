// ====== MSDF Text VS ======
[[vk::binding(0, 0)]]
cbuffer CameraUBO : register(b0, space0)
{
    float4x4 worldView;
    float4x4 worldProj;
    float4x4 screenView;
    float4x4 screenProj;

    float4   camParams;        // (fovX, fovY, zNear, zFar)
    float2   viewportSize;     // (windowWidth, windowHeight) in pixels
    float2   _padViewportSize; 
};

struct MsdfTextGpuInstanceData
{
    column_major float4x4 modelTR;

    float4   plane;        // (l,b,r,t) in EM
    float4   uvRect;       // (uL,vB,uR,vT)

    float4   fillColor;
    float4   strokeColor;

    float    strokeWidthPx;
    float    msdfPixelRange;
    float    emScalePx;
    int      space;        // 0 = world, 1 = screen

    float2   pxToModel;    // VC units per pixel (screen path)
    uint     atlasTexIndex;
    uint     _pad_;
};

[[vk::binding(3, 0)]]
StructuredBuffer<MsdfTextGpuInstanceData> gMsdfParams : register(t3, space0);

struct VSIn  { 
    float2 unit : POSITION0; 
    uint iid : SV_InstanceID; 
};

struct VSOut { 
    float4 pos  : SV_Position; 
    float2 uv : TEXCOORD0; 
    uint iid : SV_InstanceID; 
};

float2 expandToRect(float2 unit, float4 rect)  // rect=(l,b,r,t)
{
    return float2(lerp(rect.x, rect.z, unit.x), lerp(rect.y, rect.w, unit.y));
}

VSOut main(VSIn IN)
{
    VSOut OUT;
    OUT.uv  = IN.unit;     
    OUT.iid = IN.iid;

    {
        MsdfTextGpuInstanceData P = gMsdfParams[IN.iid];

        // Build glyph vertex in model space:
        float2 posEm    = expandToRect(IN.unit, P.plane); // EM
        float2 posPx    = posEm * P.emScalePx;            // EM -> px
        float2 posModel = posPx * P.pxToModel;            // px -> VC units (screen path)

        float4 local         = float4(posModel, 0.0, 1.0);
        float4 worldOrScreen = mul(P.modelTR, local);

        OUT.pos = (P.space == 0)
            ? mul(worldProj, mul(worldView, worldOrScreen))
            : mul(screenProj, worldOrScreen);
        return OUT;
    }
}
