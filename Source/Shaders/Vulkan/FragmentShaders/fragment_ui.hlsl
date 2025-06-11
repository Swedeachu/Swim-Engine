[[vk::binding(0, 1)]]
SamplerState texSampler : register(s0, space1);

[[vk::binding(1, 1)]]
Texture2D textures[] : register(t1, space1);

struct UIParams
{
  float4 fillColor;
  float4 strokeColor;
  float2 strokeWidth;
  float2 cornerRadius;
  int enableFill;
  int enableStroke;
  int roundCorners;
  int useTexture;
  float2 resolution;
  float2 quadSize;
};

[[vk::binding(2, 0)]]
StructuredBuffer<UIParams> uiParamBuffer : register(t2, space0);

struct FSInput
{
  float4 position : SV_Position;                 
  float2 uv : TEXCOORD0;                        
  nointerpolation uint textureIndex : TEXCOORD1; 
  float hasTexture : TEXCOORD2;                  
  nointerpolation uint instanceID : TEXCOORD3; // for indexing into the ui params buffer
  float3 color : TEXCOORD4;   
  float2 quadSizePx : TEXCOORD5; // half-width / half-height in pixels
  float2 centerPx : TEXCOORD6;   // pixel centre of the quad
};

float RoundedRectSDF(float2 pos, float2 size, float radius)
{
  float2 d = abs(pos) - size + radius;
  return length(max(d, 0.0f)) + min(max(d.x, d.y), 0.0f) - radius;
}

float BoxSDF(float2 pos, float2 size)
{
  float2 d = abs(pos) - size;
  return length(max(d, 0.0f)) + min(max(d.x, d.y), 0.0f);
}

float4 main(FSInput input) : SV_Target
{
    uint uiParamIndex = input.instanceID;
    UIParams ui = uiParamBuffer[uiParamIndex];

    float2 halfSize = input.quadSizePx;

    // Position in local quad space centered around (0,0)
    float2 posPx = (input.uv - 0.5f) * halfSize * 2.0f;

    // Compute screen-space-correct radius
    float radius = (ui.roundCorners != 0)
        ? min(max(ui.cornerRadius.x, ui.cornerRadius.y), min(halfSize.x, halfSize.y))
        : 0.0f;

    // Convert pixel stroke width to local-space units (0..halfSize)
    float2 strokeUV = ui.strokeWidth / (halfSize * 2.0f);

    // Fill area and radius
    float2 innerHalf = halfSize;
    float innerRad = radius;

    if (ui.enableStroke != 0)
    {
        // Adjust inner shape only if stroke is enabled
        innerHalf = max(halfSize - strokeUV * halfSize * 2.0f, float2(0.0f, 0.0f));
        innerRad = max(radius - max(strokeUV.x * halfSize.x * 2.0f, strokeUV.y * halfSize.y * 2.0f), 0.0f);
    }

    // Compute distance to outer shape
    float distOuter = (radius > 0.0f)
        ? RoundedRectSDF(posPx, halfSize, radius)
        : BoxSDF(posPx, halfSize);

    // Compute distance to inner fill shape
    float distInner = (radius > 0.0f)
        ? RoundedRectSDF(posPx, innerHalf, innerRad)
        : BoxSDF(posPx, innerHalf);

    // Screen-space correct anti-aliasing width (based on avg pixel size)
    float aaWidthPx = 1.0f;
    float aaWidth = aaWidthPx / ((halfSize.x + halfSize.y)); // average px size to local

    float outerAlpha = 1.0f - smoothstep(-aaWidth, aaWidth, distOuter);
    float innerAlpha = 1.0f - smoothstep(-aaWidth, aaWidth, distInner);

    // Stroke alpha is the shell between outer and inner SDFs
    float strokeAlpha = (ui.enableStroke != 0) ? clamp(outerAlpha - innerAlpha, 0.0f, 1.0f) : 0.0f;
    float fillAlpha = (ui.enableFill != 0) ? innerAlpha : 0.0f;

    // Sample texture if used
    float4 texSample = float4(1.0f, 1.0f, 1.0f, 1.0f);
    if (input.hasTexture > 0.5f && ui.useTexture != 0)
    {
        texSample = textures[input.textureIndex].Sample(texSampler, input.uv);
    }

    // Determine source fill color
    bool useVertexColor = all(ui.fillColor.rgb < 0.0f);
    float4 fillSrc = float4(1.0f, 1.0f, 1.0f, 1.0f);

    if (ui.useTexture != 0 && input.hasTexture > 0.5f)
    {
        fillSrc = texSample;
    }
    else if (useVertexColor)
    {
        fillSrc = float4(input.color, 1.0f);
    }
    else
    {
        fillSrc = ui.fillColor;
    }

    // Combine fill and stroke with premultiplied alpha
    float combinedAlpha = saturate(fillAlpha + strokeAlpha);
    float3 combinedColor = (combinedAlpha > 0.0f)
        ? (fillSrc.rgb * fillAlpha + ui.strokeColor.rgb * strokeAlpha) / combinedAlpha
        : float3(0.0f, 0.0f, 0.0f);

    return float4(combinedColor, combinedAlpha);
}
