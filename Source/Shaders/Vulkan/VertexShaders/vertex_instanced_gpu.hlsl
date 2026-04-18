[[vk::binding(0, 0)]]
cbuffer CameraUBO : register(b0, space0)
{
  float4x4 worldView;
  float4x4 worldProj;
  float4x4 screenView;
  float4x4 screenProj;

  float4 camParams;
  float2 viewportSize;
  float2 _padViewportSize;
};

struct GpuInstanceData
{
  float4x4 model;

  float4 aabbMin;
  float4 aabbMax;

  uint textureIndex;
  float hasTexture;
  uint meshInfoIndex;
  uint materialIndex;

  uint indexCount;
  uint space;
  uint2 vertexOffsetInMegaBuffer;
  uint2 indexOffsetInMegaBuffer;
};

// Binding 4 is the dedicated world instance packet. The GPU cull path writes compacted
// instances into this buffer so the graphics shader can consume the same layout as the CPU path.
[[vk::binding(4, 0)]]
StructuredBuffer<GpuInstanceData> instanceBuffer : register(t4, space0);

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
  float4 viewPos = mul(worldView, worldPos);
  float4 projPos = mul(worldProj, viewPos);

  output.position = projPos;
  output.color = input.color;
  output.uv = input.uv;
  output.textureIndex = instance.textureIndex;
  output.hasTexture = instance.hasTexture;

  return output;
}
