cbuffer CameraUBO : register(b0, space0) // Set 0, Binding 0
{
  float4x4 view;
  float4x4 proj;
};

struct PushConstantData
{
  float4x4 model;       // Model transform
  uint textureIndex;    // Index into bindless texture array
  float hasTexture;     // 1.0 = use texture, 0.0 = use vertex color
  float padA;
  float padB;
};

[[vk::push_constant]]
PushConstantData pc;

struct VSInput
{
  float3 position : POSITION;
  float3 color : COLOR;
  float2 uv : TEXCOORD0;
};

struct VSOutput
{
  float4 position : SV_Position;
  float3 color : COLOR;
  float2 uv : TEXCOORD0;
};

VSOutput main(VSInput input)
{
  VSOutput output;

  float4 worldPos = mul(pc.model, float4(input.position, 1.0f));
  float4 viewPos = mul(view, worldPos);
  float4 projPos = mul(proj, viewPos);

  output.position = projPos;
  output.color = input.color;
  output.uv = input.uv;

  return output;
}
