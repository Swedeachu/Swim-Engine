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

struct GpuWorldInstanceStaticData
{
  float4 boundsCenterRadius;
  uint textureIndex;
  float hasTexture;
  uint meshInfoIndex;
  uint materialIndex;
  uint indexCount;
  uint space;
  uint2 vertexOffsetInMegaBuffer;
  uint2 indexOffsetInMegaBuffer;
  uint drawCommandIndex;
  uint outputBaseInstance;
};

struct GpuWorldInstanceTransformData
{
  float4 row0;
  float4 row1;
  float4 row2;
  uint enabled;
  uint padA;
  uint padB;
  uint padC;
};

[[vk::binding(5, 0)]]
StructuredBuffer<GpuWorldInstanceStaticData> staticBuffer : register(t5, space0);

[[vk::binding(6, 0)]]
StructuredBuffer<GpuWorldInstanceTransformData> transformBuffer : register(t6, space0);

[[vk::binding(7, 0)]]
StructuredBuffer<uint> visibleIndexBuffer : register(t7, space0);

struct VSInput
{
  float3 position : POSITION;
  float3 color : COLOR;
  float2 uv : TEXCOORD0;
  uint instanceID : SV_InstanceID;
};

struct VSOutput
{
  float4 position : SV_Position;
  float3 color : COLOR;
  float2 uv : TEXCOORD0;
  uint textureIndex : TEXCOORD1;
  float hasTexture : TEXCOORD2;
};

float3 TransformPoint(float3 localPoint, GpuWorldInstanceTransformData instanceTransform)
{
  return float3(
    dot(instanceTransform.row0.xyz, localPoint) + instanceTransform.row0.w,
    dot(instanceTransform.row1.xyz, localPoint) + instanceTransform.row1.w,
    dot(instanceTransform.row2.xyz, localPoint) + instanceTransform.row2.w
  );
}

VSOutput main(VSInput input)
{
  VSOutput output;

  uint sceneInstanceIndex = visibleIndexBuffer[input.instanceID];
  GpuWorldInstanceStaticData instanceStatic = staticBuffer[sceneInstanceIndex];
  GpuWorldInstanceTransformData instanceTransform = transformBuffer[sceneInstanceIndex];

  float3 worldPos3 = TransformPoint(input.position, instanceTransform);
  float4 worldPos = float4(worldPos3, 1.0f);
  float4 viewPos = mul(worldView, worldPos);
  float4 projPos = mul(worldProj, viewPos);

  output.position = projPos;
  output.color = input.color;
  output.uv = input.uv;
  output.textureIndex = instanceStatic.textureIndex;
  output.hasTexture = instanceStatic.hasTexture;

  return output;
}
