[[vk::binding(0, 0)]]
cbuffer CameraUBO : register(b0, space0)
{
  float4x4 worldView;
  float4x4 worldProj;
  float4x4 screenView;
  float4x4 screenProj;

  float4 camParams;
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
  uint space;  // 0 = world, 1 = screen

  uint2 vertexOffsetInMegaBuffer;
  uint2 indexOffsetInMegaBuffer;
};

[[vk::binding(1, 0)]]
StructuredBuffer<GpuInstanceData> instanceBuffer : register(t1, space0);

struct VSInput
{
  float3 position : POSITION;
  float3 color : COLOR;      // Ignored for decorators
  float2 uv : TEXCOORD0;
  uint   instanceID : SV_InstanceID;
};

struct VSOutput
{
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
  uint   textureIndex : TEXCOORD1;
  float  hasTexture : TEXCOORD2;
};

VSOutput main(VSInput input)
{
  VSOutput output;

  // Fetch per-instance data
  GpuInstanceData instance = instanceBuffer[input.instanceID];

  // Choose matrices based on space (0 = world, 1 = screen)
  float4x4 viewMatrix = (instance.space == 1) ? screenView : worldView;
  float4x4 projMatrix = (instance.space == 1) ? screenProj : worldProj;

  // Transform position
  float4 worldPos = mul(instance.model, float4(input.position, 1.0f));
  float4 viewPos = mul(viewMatrix, worldPos);
  float4 clipPos = mul(projMatrix, viewPos);

  // Output values
  output.position = clipPos;
  output.uv = input.uv;
  output.textureIndex = instance.textureIndex;
  output.hasTexture = instance.hasTexture;

  return output;
}
