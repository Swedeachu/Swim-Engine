// === Bindings ===
[[vk::binding(0, 0)]]
cbuffer CameraUBO : register(b0, space0)
{
  float4x4 view;
  float4x4 proj;
  float4 camParams; // (fovX, fovY, zNear, zFar)
};

[[vk::binding(1, 0)]]
cbuffer InstanceMeta : register(b1, space0)
{
  uint instanceCount;
  uint padA;
  uint padB;
  uint padC;
};

struct GpuInstanceData
{
  float4x4 model;        // 64

  float4   aabbMin;      // 16
  float4   aabbMax;      // 16

  uint     textureIndex; // 4
  float    hasTexture;   // 4
  uint     meshIndex;    // 4
  uint     pad;          // 4
};                         // = 128

[[vk::binding(2, 0)]]
StructuredBuffer<GpuInstanceData> instanceBuffer : register(t0, space0);

[[vk::binding(3, 0)]]
RWStructuredBuffer<float4x4> visibleModels : register(u0, space0);

[[vk::binding(4, 0)]]
RWStructuredBuffer<uint4> visibleData : register(u1, space0);

[[vk::binding(5, 0)]]
RWByteAddressBuffer drawCount : register(u2, space0);

// === Frustum Visibility ===
bool IsAABBVisible(float4x4 model, float3 minCorner, float3 maxCorner)
{
  float3 corners[8] = {
      float3(minCorner.x, minCorner.y, minCorner.z),
      float3(minCorner.x, minCorner.y, maxCorner.z),
      float3(minCorner.x, maxCorner.y, minCorner.z),
      float3(minCorner.x, maxCorner.y, maxCorner.z),
      float3(maxCorner.x, minCorner.y, minCorner.z),
      float3(maxCorner.x, minCorner.y, maxCorner.z),
      float3(maxCorner.x, maxCorner.y, minCorner.z),
      float3(maxCorner.x, maxCorner.y, maxCorner.z)
  };

  float fovX = camParams.x;
  float fovY = camParams.y;
  float zNear = camParams.z;
  float zFar = camParams.w;

  float halfTanX = tan(fovX * 0.5f);
  float halfTanY = tan(fovY * 0.5f);

  [unroll]
    for (int i = 0; i < 8; ++i)
    {
      float4 world = mul(model, float4(corners[i], 1.0f));
      float4 viewSpace = mul(view, world);

      float z = -viewSpace.z;

      if (z >= zNear && z <= zFar &&
        abs(viewSpace.x) <= z * halfTanX &&
        abs(viewSpace.y) <= z * halfTanY)
      {
        return true;
      }
    }

  return false;
}

// === Entry Point ===
[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
  uint index = DTid.x;
  if (index >= instanceCount) { return; }

  GpuInstanceData data = instanceBuffer[index];

  if (!IsAABBVisible(data.model, data.aabbMin, data.aabbMax)) { return; } 

  uint visibleIndex;
  drawCount.InterlockedAdd(0, 1, visibleIndex);

  visibleModels[visibleIndex] = data.model;

  visibleData[visibleIndex] = uint4(
    data.textureIndex,
    asuint(data.hasTexture),
    0,
    data.meshIndex
  );
}
