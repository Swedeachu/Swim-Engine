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

struct GpuWorldBvhNodeData
{
  float4 minX;
  float4 minY;
  float4 minZ;
  float4 maxX;
  float4 maxY;
  float4 maxZ;
  int4 childRef;
  uint childCount;
  uint padA;
  uint padB;
  uint padC;
};

struct GpuWorldBvhLeafData
{
  uint firstRangeIndex;
  uint rangeCount;
  uint padA;
  uint padB;
};

struct GpuWorldInstanceRangeData
{
  uint start;
  uint count;
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
  uint gpuBvhNodeCount;
  uint gpuBvhRootIndex;
  uint gpuBvhMaxDepth;
  uint gpuBvhDepthOffset;
  uint gpuBvhDepthCount;
  uint padA;
  uint padB;
  uint padC;
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

[[vk::binding(7, 0)]]
StructuredBuffer<GpuWorldBvhNodeData> bvhNodes : register(t7, space0);

[[vk::binding(8, 0)]]
StructuredBuffer<GpuWorldBvhLeafData> bvhLeaves : register(t8, space0);

[[vk::binding(9, 0)]]
StructuredBuffer<GpuWorldInstanceRangeData> bvhLeafRanges : register(t9, space0);

[[vk::binding(10, 0)]]
StructuredBuffer<uint> bvhNodeDepthIndices : register(t10, space0);

[[vk::binding(11, 0)]]
StructuredBuffer<uint> bvhDepthOffsets : register(t11, space0);

[[vk::binding(12, 0)]]
RWStructuredBuffer<uint> bvhNodeStates : register(u12, space0);

[[vk::binding(13, 0)]]
RWStructuredBuffer<uint> visibleLeafIndices : register(u13, space0);

[[vk::binding(14, 0)]]
RWStructuredBuffer<uint> visibleLeafCount : register(u14, space0);

[[vk::binding(15, 0)]]
RWStructuredBuffer<uint> visibleCandidateInstanceIds : register(u15, space0);

[[vk::binding(16, 0)]]
RWStructuredBuffer<uint> visibleCandidateCount : register(u16, space0);

[[vk::binding(17, 0)]]
RWStructuredBuffer<uint> drawOffsets : register(u17, space0);

static const uint MODE_CULL_INSTANCES = 0u;
static const uint MODE_BUILD_DRAW_RANGES = 2u;
static const uint MODE_SCATTER_VISIBLE_IDS = 3u;
static const uint GROUP_SIZE = 64u;
static const float INSTANCE_SPHERE_EPSILON_MIN = 0.02f;
static const float INSTANCE_SPHERE_EPSILON_SCALE = 0.02f;

uint CountBitsBallot(uint4 mask)
{
  return countbits(mask.x) + countbits(mask.y) + countbits(mask.z) + countbits(mask.w);
}

uint FirstBitLowBallot(uint4 mask)
{
  if (mask.x != 0u)
  {
    return firstbitlow(mask.x);
  }
  if (mask.y != 0u)
  {
    return 32u + firstbitlow(mask.y);
  }
  if (mask.z != 0u)
  {
    return 64u + firstbitlow(mask.z);
  }
  return 96u + firstbitlow(mask.w);
}

uint CountBitsBeforeLane(uint4 mask, uint laneIndex)
{
  uint component = laneIndex >> 5u;
  uint bit = laneIndex & 31u;
  uint prefixCount = 0u;

  if (component > 0u)
  {
    prefixCount += countbits(mask.x);
  }
  if (component > 1u)
  {
    prefixCount += countbits(mask.y);
  }
  if (component > 2u)
  {
    prefixCount += countbits(mask.z);
  }

  uint prefixMask = (bit == 0u) ? 0u : ((1u << bit) - 1u);
  if (component == 0u)
  {
    prefixCount += countbits(mask.x & prefixMask);
  }
  else if (component == 1u)
  {
    prefixCount += countbits(mask.y & prefixMask);
  }
  else if (component == 2u)
  {
    prefixCount += countbits(mask.z & prefixMask);
  }
  else
  {
    prefixCount += countbits(mask.w & prefixMask);
  }

  return prefixCount;
}

bool SphereInFrustumConservative(GpuWorldInstanceStaticData instanceStatic, GpuWorldInstanceTransformData instanceTransform)
{
  float3 localCenter = instanceStatic.boundsCenterRadius.xyz;
  float localRadius = instanceStatic.boundsCenterRadius.w;

  float3 worldCenter = float3(
    dot(instanceTransform.row0.xyz, localCenter) + instanceTransform.row0.w,
    dot(instanceTransform.row1.xyz, localCenter) + instanceTransform.row1.w,
    dot(instanceTransform.row2.xyz, localCenter) + instanceTransform.row2.w
  );

  float3 sx = float3(instanceTransform.row0.x, instanceTransform.row1.x, instanceTransform.row2.x);
  float3 sy = float3(instanceTransform.row0.y, instanceTransform.row1.y, instanceTransform.row2.y);
  float3 sz = float3(instanceTransform.row0.z, instanceTransform.row1.z, instanceTransform.row2.z);
  float maxScale = max(length(sx), max(length(sy), length(sz)));
  float worldRadius = localRadius * maxScale;
  float frustumSlack = max(worldRadius * INSTANCE_SPHERE_EPSILON_SCALE, INSTANCE_SPHERE_EPSILON_MIN);

  [unroll]
  for (uint planeIndex = 0u; planeIndex < 6u; ++planeIndex)
  {
    float4 plane = pc.frustumPlanes[planeIndex];
    float distanceToPlane = dot(plane.xyz, worldCenter) + plane.w;
    if (distanceToPlane + worldRadius < -frustumSlack)
    {
      return false;
    }
  }

  return true;
}

[numthreads(GROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
  uint index = dispatchThreadID.x;

  if (pc.mode == MODE_CULL_INSTANCES)
  {
    if (index >= pc.instanceCount)
    {
      return;
    }

    GpuWorldInstanceTransformData instanceTransform = transformBuffer[index];
    bool visible = false;
    GpuWorldInstanceStaticData instanceStatic = staticBuffer[index];
    if (instanceTransform.enabled != 0u)
    {
      visible = SphereInFrustumConservative(instanceStatic, instanceTransform);
    }

    uint laneVisible = visible ? 1u : 0u;
    uint4 ballot = WaveActiveBallot(laneVisible != 0u);
    if (!visible)
    {
      return;
    }

    uint waveCount = CountBitsBallot(ballot);
    uint leaderLane = FirstBitLowBallot(ballot);
    uint baseCandidateIndex = 0u;
    if (WaveGetLaneIndex() == leaderLane)
    {
      InterlockedAdd(visibleCandidateCount[0], waveCount, baseCandidateIndex);
    }
    baseCandidateIndex = WaveReadLaneAt(baseCandidateIndex, leaderLane);

    uint localOffset = CountBitsBeforeLane(ballot, WaveGetLaneIndex());
    visibleCandidateInstanceIds[baseCandidateIndex + localOffset] = index;

    uint ignored = 0u;
    InterlockedAdd(drawInstanceCounts[instanceStatic.drawCommandIndex], 1u, ignored);
    return;
  }

  if (pc.mode == MODE_BUILD_DRAW_RANGES)
  {
    if (index != 0u)
    {
      return;
    }

    uint runningInstanceOffset = 0u;
    uint visibleDrawCount = 0u;
    for (uint drawIndex = 0u; drawIndex < pc.drawCommandCount; ++drawIndex)
    {
      uint instanceCount = drawInstanceCounts[drawIndex];
      drawOffsets[drawIndex] = runningInstanceOffset;

      if (instanceCount != 0u)
      {
        DrawIndexedIndirectCommand drawCommand = drawTemplateBuffer[drawIndex];
        drawCommand.firstInstance = runningInstanceOffset;
        drawCommand.instanceCount = instanceCount;
        indirectCommandBuffer[visibleDrawCount] = drawCommand;
        runningInstanceOffset += instanceCount;
        ++visibleDrawCount;
      }

      drawInstanceCounts[drawIndex] = 0u;
    }

    for (uint drawIndex = visibleDrawCount; drawIndex < pc.drawCommandCount; ++drawIndex)
    {
      DrawIndexedIndirectCommand drawCommand = drawTemplateBuffer[drawIndex];
      drawCommand.instanceCount = 0u;
      indirectCommandBuffer[drawIndex] = drawCommand;
    }

    drawCountScalar[0] = (pc.compactDraws != 0u) ? visibleDrawCount : pc.drawCommandCount;
    return;
  }

  if (pc.mode == MODE_SCATTER_VISIBLE_IDS)
  {
    uint visibleCount = visibleCandidateCount[0];
    if (index >= visibleCount)
    {
      return;
    }

    uint instanceIndex = visibleCandidateInstanceIds[index];
    uint drawKey = staticBuffer[instanceIndex].drawCommandIndex;
    uint localOffset = 0u;
    InterlockedAdd(drawInstanceCounts[drawKey], 1u, localOffset);
    visibleInstanceIds[drawOffsets[drawKey] + localOffset] = instanceIndex;
    return;
  }
}
