[[vk::binding(0, 0)]]
cbuffer CameraUBO : register(b0, space0)
{
  float4x4 view;
  float4x4 proj;
  float4 camParams; // (fovX, fovY, zNear, zFar)
};

struct GpuInstanceData
{
  float4x4 model;
  uint     textureIndex;
  float    hasTexture;
  float    padA;
  float    padB;
  uint     meshIndex;
  float    padC;
  float    padD;
  float    padE;
};

[[vk::binding(1, 0)]]
StructuredBuffer<GpuInstanceData> instanceBuffer : register(t1, space0);

struct VSInput
{
  float3 position : POSITION;
  float3 color : COLOR;
  float2 uv : TEXCOORD0;
  uint   instanceID : SV_InstanceID;
};

struct VSOutput
{
  float4 position : SV_Position;
  float3 color : COLOR;
  float2 uv : TEXCOORD0;
  uint   textureIndex : TEXCOORD1;
  float  hasTexture : TEXCOORD2;
};

VSOutput main(VSInput input)
{
  VSOutput output;

  GpuInstanceData instance = instanceBuffer[input.instanceID];

  float4 worldPos = mul(instance.model, float4(input.position, 1.0f));
  float4 viewPos = mul(view, worldPos);
  float4 projPos = mul(proj, viewPos);

  output.position = projPos;
  output.color = input.color;
  output.uv = input.uv;
  output.textureIndex = instance.textureIndex;
  output.hasTexture = instance.hasTexture;

  return output;
}
