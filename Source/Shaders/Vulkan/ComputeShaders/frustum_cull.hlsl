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
RWStructuredBuffer<GpuInstanceData> outputInstanceBuffer : register(u2, space0);

[[vk::binding(3, 0)]]
RWStructuredBuffer<uint> drawInstanceCounts : register(u3, space0);

[[vk::binding(4, 0)]]
StructuredBuffer<DrawIndexedIndirectCommand> drawTemplateBuffer : register(t4, space0);

[[vk::binding(5, 0)]]
RWStructuredBuffer<DrawIndexedIndirectCommand> indirectCommandBuffer : register(u5, space0);

static const uint MODE_CULL = 1;
static const uint MODE_FINALIZE = 2;

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

GpuInstanceData BuildOutputInstance(GpuWorldInstanceStaticData instanceStatic, GpuWorldInstanceTransformData instanceTransform)
{
  GpuInstanceData output;
  output.model = instanceTransform.model;

  float3 center = instanceStatic.boundsCenterRadius.xyz;
  float radius = instanceStatic.boundsCenterRadius.w;
  float3 extent = float3(radius, radius, radius);
  output.aabbMin = float4(center - extent, 1.0f);
  output.aabbMax = float4(center + extent, 1.0f);

  output.textureIndex = instanceStatic.textureIndex;
  output.hasTexture = instanceStatic.hasTexture;
  output.meshInfoIndex = instanceStatic.meshInfoIndex;
  output.materialIndex = instanceStatic.materialIndex;
  output.indexCount = instanceStatic.indexCount;
  output.space = instanceStatic.space;
  output.vertexOffsetInMegaBuffer = instanceStatic.vertexOffsetInMegaBuffer;
  output.indexOffsetInMegaBuffer = instanceStatic.indexOffsetInMegaBuffer;
  return output;
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
    outputInstanceBuffer[instanceStatic.outputBaseInstance + localInstanceIndex] = BuildOutputInstance(instanceStatic, instanceTransform);
    return;
  }

  if (pc.mode == MODE_FINALIZE)
  {
    if (index >= pc.drawCommandCount)
    {
      return;
    }

    DrawIndexedIndirectCommand drawCommand = drawTemplateBuffer[index];
    drawCommand.instanceCount = drawInstanceCounts[index];
    indirectCommandBuffer[index] = drawCommand;
  }
}
