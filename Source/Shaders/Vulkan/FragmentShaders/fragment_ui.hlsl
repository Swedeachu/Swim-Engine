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

[[vk::push_constant]]
UIParams ui;

struct FSInput
{
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
  uint   textureIndex : TEXCOORD1;
  float  hasTexture : TEXCOORD2;
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
    float2 uv = input.uv - 0.5f;
    float2 pos = uv * ui.quadSize;
    float2 halfSize = ui.quadSize * 0.5f;

    float radius = (ui.roundCorners != 0) ? max(ui.cornerRadius.x, ui.cornerRadius.y) : 0.0f;
    float maxRadius = min(halfSize.x, halfSize.y);
    radius = min(radius, maxRadius);

    float2 innerHalfSize = max(halfSize - ui.strokeWidth, float2(0.0f, 0.0f));
    float innerRadius = max(radius - max(ui.strokeWidth.x, ui.strokeWidth.y), 0.0f);

    float distOuter = (radius > 0.0f) ? RoundedRectSDF(pos, halfSize, radius) : BoxSDF(pos, halfSize);
    float distInner = (radius > 0.0f) ? RoundedRectSDF(pos, innerHalfSize, innerRadius) : BoxSDF(pos, innerHalfSize);

    float aaWidth = 1.0f;
    float outerAlpha = 1.0f - smoothstep(-aaWidth, aaWidth, distOuter);
    float innerAlpha = 1.0f - smoothstep(-aaWidth, aaWidth, distInner);

    float strokeAlpha = (ui.enableStroke != 0) ? clamp(outerAlpha - innerAlpha, 0.0f, 1.0f) : 0.0f;
    float fillAlpha = (ui.enableFill != 0) ? innerAlpha : 0.0f;

    float4 texSample = float4(1.0f, 1.0f, 1.0f, 1.0f);
    if (input.hasTexture > 0.5f && ui.useTexture != 0)
    {
        texSample = textures[input.textureIndex].Sample(texSampler, input.uv);
    }

    float4 fillSrc = (ui.useTexture != 0 && input.hasTexture > 0.5f) ? texSample : ui.fillColor;

    float3 finalColor = lerp(fillSrc.rgb, ui.strokeColor.rgb, strokeAlpha);
    float finalAlpha = lerp(fillSrc.a * fillAlpha, ui.strokeColor.a, strokeAlpha);

    return float4(finalColor, finalAlpha);
}
