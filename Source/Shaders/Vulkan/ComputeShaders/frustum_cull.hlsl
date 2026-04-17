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

struct GpuCullInputInstanceData
{
  GpuInstanceData instance;
  uint drawCommandIndex;
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
  uint pad;
  float4 frustumPlanes[6];
};

[[vk::push_constant]]
PushConstants pc;

[[vk::binding(0, 0)]]
StructuredBuffer<GpuCullInputInstanceData> cullInputBuffer : register(t0, space0);

[[vk::binding(1, 0)]]
RWStructuredBuffer<GpuInstanceData> visibleInstanceBuffer : register(u1, space0);

[[vk::binding(2, 0)]]
RWStructuredBuffer<uint> drawInstanceCounts : register(u2, space0);

[[vk::binding(3, 0)]]
StructuredBuffer<DrawIndexedIndirectCommand> drawTemplateBuffer : register(t3, space0);

[[vk::binding(4, 0)]]
RWStructuredBuffer<DrawIndexedIndirectCommand> indirectCommandBuffer : register(u4, space0);

static const uint MODE_RESET = 0;
static const uint MODE_CULL = 1;
static const uint MODE_FINALIZE = 2;

bool IsVisible(float4 localAabbMin, float4 localAabbMax, float4x4 model)
{
  float3 localCenter = (localAabbMin.xyz + localAabbMax.xyz) * 0.5f;
  float3 localExtents = max((localAabbMax.xyz - localAabbMin.xyz) * 0.5f, 0.0f.xxx);

  float3 worldCenter = mul(model, float4(localCenter, 1.0f)).xyz;

  float3 worldExtents =
    abs(mul((float3x3)model, float3(localExtents.x, 0.0f, 0.0f))) +
    abs(mul((float3x3)model, float3(0.0f, localExtents.y, 0.0f))) +
    abs(mul((float3x3)model, float3(0.0f, 0.0f, localExtents.z)));

  [unroll]
  for (uint planeIndex = 0; planeIndex < 6; ++planeIndex)
  {
    float4 plane = pc.frustumPlanes[planeIndex];
    float distanceToPlane = dot(plane.xyz, worldCenter) + plane.w;
    float projectedRadius = dot(abs(plane.xyz), worldExtents);

    if (distanceToPlane + projectedRadius < 0.0f)
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

  if (pc.mode == MODE_RESET)
  {
    if (index >= pc.drawCommandCount)
    {
      return;
    }

    drawInstanceCounts[index] = 0;
    return;
  }

  if (pc.mode == MODE_CULL)
  {
    if (index >= pc.instanceCount)
    {
      return;
    }

    GpuCullInputInstanceData input = cullInputBuffer[index];
    if (!IsVisible(input.instance.aabbMin, input.instance.aabbMax, input.instance.model))
    {
      return;
    }

    uint localInstanceIndex;
    InterlockedAdd(drawInstanceCounts[input.drawCommandIndex], 1, localInstanceIndex);

    DrawIndexedIndirectCommand drawTemplate = drawTemplateBuffer[input.drawCommandIndex];
    visibleInstanceBuffer[drawTemplate.firstInstance + localInstanceIndex] = input.instance;
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
