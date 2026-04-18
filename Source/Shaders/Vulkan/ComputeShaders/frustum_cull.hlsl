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
  float4x4 model;
  uint enabled;
  uint padA;
  uint padB;
  uint padC;
};

struct DrawIndexedIndirectCommand
{
  uint indexCount;
  uint instanceCount;
  uint firstIndex;
  int vertexOffset;
  uint firstInstance;
};

struct PushConstants
{
  uint mode;
  uint instanceCount;
  uint drawCommandCount;
  uint compactDraws;
  float4 frustumPlanes[6];
};

[[vk::push_constant]]
PushConstants pc;

[[vk::binding(0, 0)]]
StructuredBuffer<GpuWorldInstanceStaticData> staticBuffer : register(t0, space0);

[[vk::binding(1, 0)]]
StructuredBuffer<GpuWorldInstanceTransformData> transformBuffer : register(t1, space0);

[[vk::binding(2, 0)]]
RWStructuredBuffer<uint> visibleInstanceIds : register(u2, space0);

[[vk::binding(3, 0)]]
RWStructuredBuffer<uint> drawInstanceCounts : register(u3, space0);

[[vk::binding(4, 0)]]
StructuredBuffer<DrawIndexedIndirectCommand> drawTemplateBuffer : register(t4, space0);

[[vk::binding(5, 0)]]
RWStructuredBuffer<DrawIndexedIndirectCommand> indirectCommandBuffer : register(u5, space0);

[[vk::binding(6, 0)]]
RWStructuredBuffer<uint> drawCountScalar : register(u6, space0);

static const uint MODE_CULL = 1;
static const uint MODE_FINALIZE = 2;
static const uint MODE_COMPACT = 3;

float3x3 ExtractLinearPart(float4x4 m)
{
  return float3x3(
    m[0].xyz,
    m[1].xyz,
    m[2].xyz
  );
}

bool IsVisible(GpuWorldInstanceStaticData instanceStatic, GpuWorldInstanceTransformData instanceTransform)
{
  float3 localCenter = instanceStatic.boundsCenterRadius.xyz;
  float localRadius = instanceStatic.boundsCenterRadius.w;

  float3 worldCenter = mul(instanceTransform.model, float4(localCenter, 1.0f)).xyz;

  float3x3 linearPart = ExtractLinearPart(instanceTransform.model);
  float maxScale = max(length(linearPart[0]), max(length(linearPart[1]), length(linearPart[2])));
  float worldRadius = localRadius * maxScale;

  [unroll]
  for (uint planeIndex = 0; planeIndex < 6; ++planeIndex)
  {
    float4 plane = pc.frustumPlanes[planeIndex];
    float distanceToPlane = dot(plane.xyz, worldCenter) + plane.w;
    if (distanceToPlane + worldRadius < 0.0f)
    {
      return false;
    }
  }

  return true;
}

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
  uint index = dispatchThreadID.x;

  if (pc.mode == MODE_CULL)
  {
    if (index >= pc.instanceCount)
    {
      return;
    }

    GpuWorldInstanceTransformData instanceTransform = transformBuffer[index];
    if (instanceTransform.enabled == 0)
    {
      return;
    }

    GpuWorldInstanceStaticData instanceStatic = staticBuffer[index];
    if (!IsVisible(instanceStatic, instanceTransform))
    {
      return;
    }

    uint localInstanceIndex = 0;
    InterlockedAdd(drawInstanceCounts[instanceStatic.drawCommandIndex], 1, localInstanceIndex);

    DrawIndexedIndirectCommand drawTemplate = drawTemplateBuffer[instanceStatic.drawCommandIndex];
    visibleInstanceIds[drawTemplate.firstInstance + localInstanceIndex] = index;
    return;
  }

  if (index >= pc.drawCommandCount)
  {
    return;
  }

  DrawIndexedIndirectCommand drawCommand = drawTemplateBuffer[index];
  drawCommand.instanceCount = drawInstanceCounts[index];

  if (pc.mode == MODE_FINALIZE)
  {
    indirectCommandBuffer[index] = drawCommand;
    return;
  }

  if (pc.mode == MODE_COMPACT)
  {
    if (drawCommand.instanceCount == 0)
    {
      return;
    }

    uint compactedIndex = 0;
    InterlockedAdd(drawCountScalar[0], 1, compactedIndex);
    indirectCommandBuffer[compactedIndex] = drawCommand;
  }
}
