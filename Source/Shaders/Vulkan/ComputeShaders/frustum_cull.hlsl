// === [b0] Camera UBO ===
// Supplied via descriptorManager->UpdatePerFrameUBO()
// Contains view & projection matrices and camera parameters
[[vk::binding(0, 0)]]
cbuffer CameraUBO : register(b0, space0)
{
  float4x4 view;        // world -> view matrix
  float4x4 proj;        // view -> clip matrix (with Vulkan Y-flip already applied)
  float4   camParams;   // (tanHalfFovX, tanHalfFovY, nearClip, farClip)
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

// === Extract camera position from view matrix ===
float3 ExtractCameraPositionFromViewMatrix(float4x4 viewMatrix)
{
  // The inverse of the rotation part of the view matrix
  float3x3 rotInv = float3x3(
    viewMatrix[0][0], viewMatrix[1][0], viewMatrix[2][0],
    viewMatrix[0][1], viewMatrix[1][1], viewMatrix[2][1],
    viewMatrix[0][2], viewMatrix[1][2], viewMatrix[2][2]
  );

  // The camera position is -transpose(rotationPart) * translation
  float3 camPos = -mul(rotInv, float3(viewMatrix[3][0], viewMatrix[3][1], viewMatrix[3][2]));
  return camPos;
}

// Debug version - very simple and permissive culling for testing
bool IsVisibleSimple(float4 localMin, float4 localMax, float4x4 model)
{
  // Calculate center point in local space
  float3 localCenter = (localMin.xyz + localMax.xyz) * 0.5f;

  // Transform to world space
  float3 worldCenter = mul(model, float4(localCenter, 1.0f)).xyz;

  // Very large radius - almost no culling, just to test pipeline
  float radius = 1000.0f;

  // Simple distance test
  float3 camPos = ExtractCameraPositionFromViewMatrix(view);
  float dist = length(worldCenter - camPos);

  // Only cull things very far away
  return (dist < radius);
}

// For debugging - mark everything as visible
bool IsVisibleAll()
{
  return true;
}

// Sphere-based frustum culling that handles camera position and rotation
bool IsVisibleSphere(float4 localMin, float4 localMax, float4x4 model, float4x4 viewMatrix,
  float tanHalfFovX, float tanHalfFovY, float nearClip, float farClip)
{
  // Calculate the center and radius of a bounding sphere around the AABB
  float3 localCenter = (localMin.xyz + localMax.xyz) * 0.5f;
  float3 localExtent = localMax.xyz - localMin.xyz;
  float localRadius = length(localExtent) * 0.5f;

  // Transform center to world space
  float3 worldCenter = mul(model, float4(localCenter, 1.0f)).xyz;

  // Get scale factor from model matrix (approximate)
  float3 scaleX = float3(model[0][0], model[0][1], model[0][2]);
  float3 scaleY = float3(model[1][0], model[1][1], model[1][2]);
  float3 scaleZ = float3(model[2][0], model[2][1], model[2][2]);

  float maxScale = max(length(scaleX), max(length(scaleY), length(scaleZ)));
  float worldRadius = localRadius * maxScale;

  // Transform center to view space
  float3 viewCenter = mul(viewMatrix, float4(worldCenter, 1.0f)).xyz;

  // Check if behind near plane accounting for radius
  if (viewCenter.z - worldRadius > nearClip)
  {
    return false;
  }

  // Check if beyond far plane accounting for radius
  if (viewCenter.z + worldRadius < -farClip)
  {
    return false;
  }

  // For objects intersecting the near plane, we need special handling
  // since projection math gets wonky there - be conservative and include them
  if (viewCenter.z > 0.0f)
  {
    // Sphere center is behind camera, check if it intersects the near plane
    if (viewCenter.z - worldRadius < 0.0f)
    {
      // Conservative approach - accept it
      return true;
    }
    return false; // Entirely behind camera
  }

  // At this point we know the sphere center is in front of the camera (negative Z)
  // Check against the side planes

  // Left and right planes - check if sphere is completely outside
  if (viewCenter.x - worldRadius > -viewCenter.z * tanHalfFovX ||
    viewCenter.x + worldRadius < viewCenter.z * tanHalfFovX)
  {
    return false;
  }

  // Top and bottom planes - check if sphere is completely outside
  if (viewCenter.y - worldRadius > -viewCenter.z * tanHalfFovY ||
    viewCenter.y + worldRadius < viewCenter.z * tanHalfFovY)
  {
    return false;
  }

  // All tests passed, should be visible
  return true;
}

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
  uint index = DTid.x;
  if (index >= instanceCount) { return; }

  GpuInstanceData data = instanceBuffer[index];

  // Extract parameters for frustum culling
  float tanHalfFovX = camParams.x;
  float tanHalfFovY = camParams.y;
  float nearClip = camParams.z;
  float farClip = camParams.w;

  // Start with the simplest test first for debugging (still weird flashing everywhere happening)
  // bool visible = IsVisibleAll();

  // Try simple distance-based culling next (broken)
  bool visible = IsVisibleSimple(data.aabbMin, data.aabbMax, data.model);

  // Then move to sphere-based frustum test (same kind of broken)
  // bool visible = IsVisibleSphere(data.aabbMin, data.aabbMax, data.model, view, tanHalfFovX, tanHalfFovY, nearClip, farClip);

  if (!visible)
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