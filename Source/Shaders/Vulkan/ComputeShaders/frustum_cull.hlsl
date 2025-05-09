// === [b0] Camera UBO ===
// Supplied via descriptorManager->UpdatePerFrameUBO()
// Contains view & projection matrices and camera parameters
[[vk::binding(0, 0)]]
cbuffer CameraUBO : register(b0, space0)
{
  float4x4 view;        // world -> view matrix
  float4x4 proj;        // view -> clip matrix (with Vulkan Y-flip already applied)
  float4   camParams;   // x = tan(FOVx/2), y = tan(FOVy/2), z = near, w = far
};

// === [b1] Instance Meta UBO ===
// Supplied via instanceMetaBuffer — set to instanceCount and padding
[[vk::binding(1, 0)]]
cbuffer InstanceMeta : register(b1, space0)
{
  uint instanceCount;  // Number of instances to process
  uint padA;
  uint padB;
  uint padC;
};

// === Struct that matches per-instance GPU data ===
struct GpuInstanceData
{
  float4x4 model;        // world transform matrix
  float4   aabbMin;      // AABB min in local space
  float4   aabbMax;      // AABB max in local space
  uint     textureIndex;
  float    hasTexture;
  uint     meshIndex;
  uint     pad;
};

// === [t0] Instance Buffer ===
// StructuredBuffer containing all GPU-visible instance data
// Supplied from indexDraw->GetInstanceBuffer()->GetPerFrameBuffers()
[[vk::binding(2, 0)]]
StructuredBuffer<GpuInstanceData> instanceBuffer : register(t0, space0);

// === [u0] Visible Models Buffer ===
// RWStructuredBuffer of float4x4s for GPU-visible model matrices (instancing)
// Matches Vulkan buffer: VulkanIndexDraw::visibleModelBuffer
[[vk::binding(3, 0)]]
RWStructuredBuffer<float4x4> visibleModels : register(u0, space0);

// === [u1] Visible Data Buffer ===
// RWStructuredBuffer of uint4s for texture index, mesh ID, etc.
// Matches Vulkan buffer: VulkanIndexDraw::visibleDataBuffer
[[vk::binding(4, 0)]]
RWStructuredBuffer<uint4> visibleData : register(u1, space0);

// === [u2] Draw Count Buffer ===
// RWByteAddressBuffer for atomic counter (num visible instances)
// Matches Vulkan buffer: VulkanIndexDraw::drawCountBuffer
[[vk::binding(5, 0)]]
RWByteAddressBuffer drawCount : register(u2, space0);

// === Simple Frustum Culling Function ===
// View-cone approximation using AABB center in view space
bool IsVisible(float4x4 model, float3 aabbMin, float3 aabbMax, float4x4 view, float2 tanFov)
{
  // Compute AABB center in local space
  float3 localCenter = 0.5 * (aabbMin + aabbMax);

  // Transform to world space
  float4 worldCenter = mul(model, float4(localCenter, 1.0));

  // Transform to view space (camera space)
  float4 viewPos = mul(view, worldCenter);

  // Flip Z for Vulkan's convention (forward = negative Z)
  float zView = -viewPos.z;

  // Cull if behind near plane
  if (zView < camParams.z) { return false; }

  // Frustum cone check: X and Y within angular bounds
  float maxX = zView * tanFov.x;
  float maxY = zView * tanFov.y;

  if (abs(viewPos.x) > maxX) { return false; }
  if (abs(viewPos.y) > maxY) { return false; }

  return true;
}

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
  uint index = DTid.x;
  if (index >= instanceCount) { return; }

  GpuInstanceData data = instanceBuffer[index];

  // Simple culling test using AABB center in view space
  float2 tanFov = float2(camParams.x, camParams.y);

  if (!IsVisible(data.model, data.aabbMin.xyz, data.aabbMax.xyz, view, tanFov))
  {
    return; // culled — not visible
  }

  // Append visible instance
  uint visibleIndex;
  drawCount.InterlockedAdd(0, 1, visibleIndex);

  // Bounds check to prevent out-of-bounds write
  if (visibleIndex >= 10240)
  {
    return;
  }

  visibleModels[visibleIndex] = data.model;

  visibleData[visibleIndex] = uint4(
    data.textureIndex,
    asuint(data.hasTexture),
    0u,
    data.meshIndex
  );
}
