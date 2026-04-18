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
static const uint GROUP_SIZE = 64;
static const uint INVALID_BUCKET_KEY = 0xffffffffu;

// Workgroup-local aggregation drastically reduces the number of global atomics in the hot cull pass.
// The scene packet is already sorted by mesh/draw key on the CPU, so consecutive lanes in a thread group
// commonly hit the same draw bucket. We exploit that here by binning visible instances per workgroup,
// issuing one global atomic add per bucket, and then scattering the visible IDs inside the reserved range.
groupshared uint groupBucketKeys[GROUP_SIZE];
groupshared uint groupBucketCounts[GROUP_SIZE];
groupshared uint groupBucketBaseOffsets[GROUP_SIZE];
groupshared uint groupBucketScatterOffsets[GROUP_SIZE];

float3 TransformPoint(float3 localPoint, GpuWorldInstanceTransformData instanceTransform)
{
  return float3(
    dot(instanceTransform.row0.xyz, localPoint) + instanceTransform.row0.w,
    dot(instanceTransform.row1.xyz, localPoint) + instanceTransform.row1.w,
    dot(instanceTransform.row2.xyz, localPoint) + instanceTransform.row2.w
  );
}

float3 LoadAxisX(GpuWorldInstanceTransformData instanceTransform)
{
  return float3(instanceTransform.row0.x, instanceTransform.row1.x, instanceTransform.row2.x);
}

float3 LoadAxisY(GpuWorldInstanceTransformData instanceTransform)
{
  return float3(instanceTransform.row0.y, instanceTransform.row1.y, instanceTransform.row2.y);
}

float3 LoadAxisZ(GpuWorldInstanceTransformData instanceTransform)
{
  return float3(instanceTransform.row0.z, instanceTransform.row1.z, instanceTransform.row2.z);
}

bool IsVisible(GpuWorldInstanceStaticData instanceStatic, GpuWorldInstanceTransformData instanceTransform)
{
  float3 localCenter = instanceStatic.boundsCenterRadius.xyz;
  float localRadius = instanceStatic.boundsCenterRadius.w;

  float3 worldCenter = TransformPoint(localCenter, instanceTransform);

  float maxScale = max(length(LoadAxisX(instanceTransform)), max(length(LoadAxisY(instanceTransform)), length(LoadAxisZ(instanceTransform))));
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

uint HashDrawKey(uint drawKey)
{
  return (drawKey * 2654435761u) & (GROUP_SIZE - 1u);
}

uint FindOrInsertBucket(uint drawKey)
{
  uint bucketIndex = HashDrawKey(drawKey);

  [unroll]
  for (uint probe = 0; probe < GROUP_SIZE; ++probe)
  {
    uint slot = (bucketIndex + probe) & (GROUP_SIZE - 1u);
    uint existingKey = INVALID_BUCKET_KEY;
    InterlockedCompareExchange(groupBucketKeys[slot], INVALID_BUCKET_KEY, drawKey, existingKey);

    if (existingKey == INVALID_BUCKET_KEY || existingKey == drawKey)
    {
      return slot;
    }
  }

  return INVALID_BUCKET_KEY;
}

[numthreads(GROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID)
{
  uint index = dispatchThreadID.x;

  if (pc.mode == MODE_CULL)
  {
    groupBucketKeys[groupThreadID.x] = INVALID_BUCKET_KEY;
    groupBucketCounts[groupThreadID.x] = 0;
    groupBucketBaseOffsets[groupThreadID.x] = 0;
    groupBucketScatterOffsets[groupThreadID.x] = 0;

    GroupMemoryBarrierWithGroupSync();

    bool laneVisible = false;
    uint bucketIndex = INVALID_BUCKET_KEY;
    uint outputBaseInstance = 0;

    if (index < pc.instanceCount)
    {
      GpuWorldInstanceTransformData instanceTransform = transformBuffer[index];
      if (instanceTransform.enabled != 0)
      {
        GpuWorldInstanceStaticData instanceStatic = staticBuffer[index];
        if (IsVisible(instanceStatic, instanceTransform))
        {
          laneVisible = true;
          outputBaseInstance = instanceStatic.outputBaseInstance;
          bucketIndex = FindOrInsertBucket(instanceStatic.drawCommandIndex);
          if (bucketIndex != INVALID_BUCKET_KEY)
          {
            uint ignoredCount = 0;
            InterlockedAdd(groupBucketCounts[bucketIndex], 1, ignoredCount);
          }
          else
          {
            laneVisible = false;
          }
        }
      }
    }

    GroupMemoryBarrierWithGroupSync();

    if (groupBucketKeys[groupThreadID.x] != INVALID_BUCKET_KEY && groupBucketCounts[groupThreadID.x] > 0)
    {
      InterlockedAdd(
        drawInstanceCounts[groupBucketKeys[groupThreadID.x]],
        groupBucketCounts[groupThreadID.x],
        groupBucketBaseOffsets[groupThreadID.x]
      );
    }

    GroupMemoryBarrierWithGroupSync();

    if (laneVisible)
    {
      uint localOffset = 0;
      InterlockedAdd(groupBucketScatterOffsets[bucketIndex], 1, localOffset);
      visibleInstanceIds[outputBaseInstance + groupBucketBaseOffsets[bucketIndex] + localOffset] = index;
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
