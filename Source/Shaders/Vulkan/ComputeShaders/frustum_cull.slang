struct GpuWorldInstanceStaticData
{
  float4 boundsCenterRadius;
  uint textureIndex;
  uint drawCommandIndex;
  uint flags;
  uint padA;
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
  uint childOrderPacked;
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

[[vk::binding(18, 0)]]
RWStructuredBuffer<uint> dispatchArgs : register(u18, space0);

static const uint MODE_TRAVERSE_BVH_DEPTH = 0u;
static const uint MODE_EXPAND_VISIBLE_LEAVES = 1u;
static const uint MODE_BUILD_DRAW_RANGES = 2u;
static const uint MODE_SCATTER_VISIBLE_IDS = 3u;
static const uint MODE_BUILD_DISPATCH_ARGS = 4u;
static const uint MODE_CULL_INSTANCES_FLAT = 5u;
static const uint GROUP_SIZE = 64u;
static const float INSTANCE_SPHERE_EPSILON_MIN = 0.02f;
static const float INSTANCE_SPHERE_EPSILON_SCALE = 0.02f;
static const float NODE_AABB_EPSILON_MIN = 0.02f;
static const float NODE_AABB_EPSILON_SCALE = 0.02f;
static const int INVALID_BVH_CHILD_REF = (-2147483647 - 1);

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

uint GetPackedChildOrder(GpuWorldBvhNodeData node, uint orderIndex)
{
  return (node.childOrderPacked >> (orderIndex * 8u)) & 0xffu;
}

bool IsEncodedLeaf(int childRef)
{
  return childRef < 0 && childRef != INVALID_BVH_CHILD_REF;
}

uint DecodeLeafRef(int childRef)
{
  return uint((-childRef) - 1);
}

bool AabbInFrustumConservative(float3 aabbMin, float3 aabbMax)
{
  float3 extents = max((aabbMax - aabbMin) * 0.5f, 0.0f.xxx);
  float halfDiagonal = length(extents);

  [unroll]
  for (uint planeIndex = 0u; planeIndex < 6u; ++planeIndex)
  {
    float4 plane = pc.frustumPlanes[planeIndex];
    float3 support = float3(
      plane.x >= 0.0f ? aabbMax.x : aabbMin.x,
      plane.y >= 0.0f ? aabbMax.y : aabbMin.y,
      plane.z >= 0.0f ? aabbMax.z : aabbMin.z
    );

    float planeLength = max(length(plane.xyz), 1.0e-6f);
    float frustumSlack = max(halfDiagonal * NODE_AABB_EPSILON_SCALE, NODE_AABB_EPSILON_MIN) * planeLength;
    if (dot(plane.xyz, support) + plane.w < -frustumSlack)
    {
      return false;
    }
  }

  return true;
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

void AppendVisibleCandidate(uint instanceIndex)
{
  uint candidateOffset = 0u;
  InterlockedAdd(visibleCandidateCount[0], 1u, candidateOffset);
  visibleCandidateInstanceIds[candidateOffset] = instanceIndex;

  uint ignored = 0u;
  InterlockedAdd(drawInstanceCounts[staticBuffer[instanceIndex].drawCommandIndex], 1u, ignored);
}

[numthreads(GROUP_SIZE, 1, 1)]
[shader("compute")]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
  uint index = dispatchThreadID.x;

  if (pc.mode == MODE_TRAVERSE_BVH_DEPTH)
  {
    if (pc.gpuBvhDepthOffset >= pc.gpuBvhDepthCount)
    {
      return;
    }

    uint depthStart = bvhDepthOffsets[pc.gpuBvhDepthOffset];
    uint depthEnd = bvhDepthOffsets[pc.gpuBvhDepthOffset + 1u];
    uint depthCount = depthEnd - depthStart;
    if (index >= depthCount)
    {
      return;
    }

    uint nodeIndex = bvhNodeDepthIndices[depthStart + index];
    if (pc.gpuBvhDepthOffset > 0u && bvhNodeStates[nodeIndex] == 0u)
    {
      return;
    }

    GpuWorldBvhNodeData node = bvhNodes[nodeIndex];
    for (uint orderedChildIndex = 0u; orderedChildIndex < node.childCount; ++orderedChildIndex)
    {
      uint childSlot = GetPackedChildOrder(node, orderedChildIndex);
      if (childSlot >= node.childCount)
      {
        continue;
      }

      int childRef = node.childRef[childSlot];
      if (childRef == INVALID_BVH_CHILD_REF)
      {
        continue;
      }

      float3 childMin = float3(node.minX[childSlot], node.minY[childSlot], node.minZ[childSlot]);
      float3 childMax = float3(node.maxX[childSlot], node.maxY[childSlot], node.maxZ[childSlot]);
      if (!AabbInFrustumConservative(childMin, childMax))
      {
        continue;
      }

      if (IsEncodedLeaf(childRef))
      {
        uint leafOffset = 0u;
        InterlockedAdd(visibleLeafCount[0], 1u, leafOffset);
        visibleLeafIndices[leafOffset] = DecodeLeafRef(childRef);
      }
      else
      {
        bvhNodeStates[uint(childRef)] = 1u;
      }
    }

    return;
  }

  if (pc.mode == MODE_EXPAND_VISIBLE_LEAVES)
  {
    uint leafCount = visibleLeafCount[0];
    if (index >= leafCount)
    {
      return;
    }

    uint leafIndex = visibleLeafIndices[index];
    GpuWorldBvhLeafData leaf = bvhLeaves[leafIndex];
    for (uint rangeIndex = 0u; rangeIndex < leaf.rangeCount; ++rangeIndex)
    {
      GpuWorldInstanceRangeData range = bvhLeafRanges[leaf.firstRangeIndex + rangeIndex];
      uint rangeEnd = range.start + range.count;
      for (uint instanceIndex = range.start; instanceIndex < rangeEnd; ++instanceIndex)
      {
        GpuWorldInstanceTransformData instanceTransform = transformBuffer[instanceIndex];
        if (instanceTransform.enabled == 0u)
        {
          continue;
        }

        GpuWorldInstanceStaticData instanceStatic = staticBuffer[instanceIndex];
        if (!SphereInFrustumConservative(instanceStatic, instanceTransform))
        {
          continue;
        }

        AppendVisibleCandidate(instanceIndex);
      }
    }

    return;
  }

  if (pc.mode == MODE_CULL_INSTANCES_FLAT)
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

  if (pc.mode == MODE_BUILD_DISPATCH_ARGS)
  {
    if (index != 0u)
    {
      return;
    }

    uint visibleCount = visibleCandidateCount[0];
    dispatchArgs[0] = max((visibleCount + (GROUP_SIZE - 1u)) / GROUP_SIZE, 1u);
    dispatchArgs[1] = 1u;
    dispatchArgs[2] = 1u;
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
