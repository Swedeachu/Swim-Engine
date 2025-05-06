[[vk::binding(0, 0)]]
cbuffer CameraUBO : register(b0, space0)
{
  float4x4 view;
  float4x4 proj;
  float4 camParams; // (fovX, fovY, zNear, zFar)
};

struct GpuInstanceData
{
  float4x4 model;        // 64

  float4   aabbMin;      // 16
  float4   aabbMax;      // 16

  uint     textureIndex; // 4
  float    hasTexture;   // 4
  uint     meshIndex;    // 4
  uint     pad;          // 4
};                         // = 128

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
