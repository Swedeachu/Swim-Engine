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
  uint inputQueueIndex;
  uint gpuBvhNodeCount;
  uint gpuBvhRootIndex;
  uint gpuBvhMaxDepth;
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
RWStructuredBuffer<uint> traversalQueueA : register(u10, space0);

[[vk::binding(11, 0)]]
RWStructuredBuffer<uint> traversalQueueB : register(u11, space0);

[[vk::binding(12, 0)]]
RWStructuredBuffer<uint> traversalQueueCounts : register(u12, space0);

static const uint MODE_RESET = 0;
static const uint MODE_TRAVERSE = 1;
static const uint MODE_FINALIZE = 2;
static const uint MODE_COMPACT = 3;
static const uint GROUP_SIZE = 64;
static const int INVALID_CHILD_REF = -2147483648;
static const float NODE_FRUSTUM_EPSILON_MIN = 0.02f;
static const float NODE_FRUSTUM_EPSILON_SCALE = 0.01f;
static const float INSTANCE_SPHERE_EPSILON_MIN = 0.02f;
static const float INSTANCE_SPHERE_EPSILON_SCALE = 0.02f;

bool IsEncodedLeaf(int childRef)
{
  return childRef < 0 && childRef != INVALID_CHILD_REF;
}

uint DecodeLeafIndex(int childRef)
{
  return (uint)(-childRef - 1);
}

uint PackTraversalNode(uint nodeIndex, bool fullyInside)
{
  return (nodeIndex & 0x7fffffffu) | (fullyInside ? 0x80000000u : 0u);
}

float GetChildComponent(float4 v, uint childIndex)
{
  if (childIndex == 0)
  {
    return v.x;
  }
  if (childIndex == 1)
  {
    return v.y;
  }
  if (childIndex == 2)
  {
    return v.z;
  }
  return v.w;
}

bool ClassifyChildAabb(GpuWorldBvhNodeData node, uint childIndex, out bool fullyInside)
{
  float3 aabbMin = float3(
    GetChildComponent(node.minX, childIndex),
    GetChildComponent(node.minY, childIndex),
    GetChildComponent(node.minZ, childIndex)
  );

  float3 aabbMax = float3(
    GetChildComponent(node.maxX, childIndex),
    GetChildComponent(node.maxY, childIndex),
    GetChildComponent(node.maxZ, childIndex)
  );

  float3 aabbExtent = max(aabbMax - aabbMin, float3(0.0f, 0.0f, 0.0f));
  float frustumSlack = max(max(aabbExtent.x, max(aabbExtent.y, aabbExtent.z)) * NODE_FRUSTUM_EPSILON_SCALE, NODE_FRUSTUM_EPSILON_MIN);

  fullyInside = true;

  [unroll]
    for (uint planeIndex = 0; planeIndex < 6; ++planeIndex)
    {
      float4 plane = pc.frustumPlanes[planeIndex];
      float3 positiveVertex = float3(
        plane.x >= 0.0f ? aabbMax.x : aabbMin.x,
        plane.y >= 0.0f ? aabbMax.y : aabbMin.y,
        plane.z >= 0.0f ? aabbMax.z : aabbMin.z
      );

      if (dot(plane.xyz, positiveVertex) + plane.w < -frustumSlack)
      {
        fullyInside = false;
        return false;
      }

      float3 negativeVertex = float3(
        plane.x >= 0.0f ? aabbMin.x : aabbMax.x,
        plane.y >= 0.0f ? aabbMin.y : aabbMax.y,
        plane.z >= 0.0f ? aabbMin.z : aabbMax.z
      );

      if (dot(plane.xyz, negativeVertex) + plane.w < frustumSlack)
      {
        fullyInside = false;
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
    for (uint planeIndex = 0; planeIndex < 6; ++planeIndex)
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

  if (pc.mode == MODE_RESET)
  {
    if (index == 0)
    {
      traversalQueueCounts[0] = pc.gpuBvhNodeCount > 0 ? 1u : 0u;
      traversalQueueCounts[1] = 0u;
      if (pc.gpuBvhNodeCount > 0)
      {
        traversalQueueA[0] = PackTraversalNode(pc.gpuBvhRootIndex, false);
      }
    }
    return;
  }

  if (pc.mode == MODE_TRAVERSE)
  {
    uint inputQueueIndex = pc.inputQueueIndex & 1u;
    uint outputQueueIndex = inputQueueIndex ^ 1u;
    uint inputCount = traversalQueueCounts[inputQueueIndex];
    if (index >= inputCount)
    {
      return;
    }

    uint packedNode = inputQueueIndex == 0 ? traversalQueueA[index] : traversalQueueB[index];
    bool parentFullyInside = (packedNode & 0x80000000u) != 0u;
    uint nodeIndex = packedNode & 0x7fffffffu;
    if (nodeIndex >= pc.gpuBvhNodeCount)
    {
      return;
    }

    GpuWorldBvhNodeData node = bvhNodes[nodeIndex];
    [unroll]
      for (uint childIndex = 0; childIndex < 4; ++childIndex)
      {
        if (childIndex >= node.childCount)
        {
          break;
        }

        int childRef = node.childRef[childIndex];
        if (childRef == INVALID_CHILD_REF)
        {
          continue;
        }

        bool childFullyInside = parentFullyInside;
        bool childVisible = parentFullyInside;
        if (!parentFullyInside)
        {
          childVisible = ClassifyChildAabb(node, childIndex, childFullyInside);
        }

        if (!childVisible)
        {
          continue;
        }

        if (IsEncodedLeaf(childRef))
        {
          uint leafIndex = DecodeLeafIndex(childRef);
          GpuWorldBvhLeafData leaf = bvhLeaves[leafIndex];
          for (uint rangeIndex = 0; rangeIndex < leaf.rangeCount; ++rangeIndex)
          {
            GpuWorldInstanceRangeData instanceRange = bvhLeafRanges[leaf.firstRangeIndex + rangeIndex];
            uint rangeEnd = instanceRange.start + instanceRange.count;
            for (uint instanceIndex = instanceRange.start; instanceIndex < rangeEnd; ++instanceIndex)
            {
              GpuWorldInstanceTransformData instanceTransform = transformBuffer[instanceIndex];
              if (instanceTransform.enabled == 0u)
              {
                continue;
              }

              GpuWorldInstanceStaticData instanceStatic = staticBuffer[instanceIndex];
              bool instanceVisible = childFullyInside || SphereInFrustumConservative(instanceStatic, instanceTransform);
              if (!instanceVisible)
              {
                continue;
              }

              uint outputOffset = 0;
              InterlockedAdd(drawInstanceCounts[instanceStatic.drawCommandIndex], 1, outputOffset);
              visibleInstanceIds[instanceStatic.outputBaseInstance + outputOffset] = instanceIndex;
            }
          }
        }
        else
        {
          uint outputNodeIndex = 0;
          InterlockedAdd(traversalQueueCounts[outputQueueIndex], 1, outputNodeIndex);
          uint packedChild = PackTraversalNode((uint)childRef, childFullyInside);
          if (outputQueueIndex == 0)
          {
            traversalQueueA[outputNodeIndex] = packedChild;
          }
          else
          {
            traversalQueueB[outputNodeIndex] = packedChild;
          }
        }
      }
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
