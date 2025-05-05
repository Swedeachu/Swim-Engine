// Binding 0 — Camera UBO
[[vk::binding(0, 0)]]
cbuffer CameraUBO : register(b0, space0)
{
  float4x4 view;
  float4x4 proj;
  float4   camParams;   // (fovX, fovY, zNear, zFar)
};

// Binding 1 — Instance Meta (instance count)
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
  float4x4 model;
  uint     textureIndex;
  float    hasTexture;
  float    padA;
  float    padB;
  uint     meshIndex;  
  float    padC;
  float    padD;
  float    padE;
};

// Binding 2 — instanceBuffer (read-only SSBO)
[[vk::binding(2, 0)]]
StructuredBuffer<GpuInstanceData> instanceBuffer : register(t0, space0);

// Binding 3 — visibleModels (RWStructuredBuffer<float4x4>)
[[vk::binding(3, 0)]]
RWStructuredBuffer<float4x4> visibleModels : register(u0, space0);

// Binding 4 — visibleData (RWStructuredBuffer<uint4>)
[[vk::binding(4, 0)]]
RWStructuredBuffer<uint4> visibleData : register(u1, space0);

// Binding 5 — drawCount (RWByteAddressBuffer)
[[vk::binding(5, 0)]]
RWByteAddressBuffer drawCount : register(u2, space0);

// === Visibility Test ===
bool IsVisible(float3 centerVS)
{
  float fovX = camParams.x;
  float fovY = camParams.y;
  float zNear = camParams.z;
  float zFar = camParams.w;

  // Frustum test assumes camera looks down -Z (right-handed NDC)
  if (centerVS.z <= 0.0f) { return false; }

  float halfTanX = tan(fovX * 0.5f);
  float halfTanY = tan(fovY * 0.5f);

  if (abs(centerVS.x) > centerVS.z * halfTanX) { return false; }
  if (abs(centerVS.y) > centerVS.z * halfTanY) { return false; }

  return centerVS.z >= zNear && centerVS.z <= zFar;
}

// === Entry Point ===
[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
  // Debug write: force index 0 to be nonzero
  // drawCount.Store(0, asuint(69420));

  uint index = DTid.x;
  if (index >= instanceCount) { return; }

  GpuInstanceData data = instanceBuffer[index];

  // Compute world-space center, then view-space
  float4 centerWS = mul(data.model, float4(0, 0, 0, 1));
  float4 centerVS = mul(view, centerWS);

  if (!IsVisible(centerVS.xyz)) { return; }

  uint visibleIndex;
  drawCount.InterlockedAdd(0, 1, visibleIndex);

  // Store the visible transform
  visibleModels[visibleIndex] = data.model;

  // Store texture + mesh info in uint4
  visibleData[visibleIndex] = uint4(
    data.textureIndex,           // x
    asuint(data.hasTexture),     // y
    0,                           // z (pad)
    data.meshIndex               // w 
  );
}
