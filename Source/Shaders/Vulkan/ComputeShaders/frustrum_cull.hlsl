// Descriptor Set 0
cbuffer CameraUBO : register(b0, space0)
{
  float4x4 view;
  float4x4 proj;
};

cbuffer InstanceMeta : register(b1, space0)
{
  uint instanceCount;
};

StructuredBuffer<float4x4> instanceModels : register(t0, space0);
StructuredBuffer<uint4>    instanceData   : register(t1, space0);

RWStructuredBuffer<float4x4> visibleModels : register(u0, space0);
RWStructuredBuffer<uint4>    visibleData   : register(u1, space0);

RWByteAddressBuffer drawCount : register(u2, space0);

// This is using hardcoded fov values that might not match up with our renderer's camera values
// The camera UBO should probably supply fov values
bool IsVisible(float3 centerVS)
{
  if (abs(centerVS.x) > centerVS.z * tan(radians(45.0))) { return false; }
  if (abs(centerVS.y) > centerVS.z * tan(radians(30.0))) { return false; }
  if (centerVS.z < 0.1f || centerVS.z > 100.0f) { return false; }
  return true;
}

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
  uint index = DTid.x;
  if (index >= instanceCount) return;

  float4x4 model = instanceModels[index];
  float4 centerWS = mul(model, float4(0, 0, 0, 1));
  float4 centerVS = mul(view, centerWS);

  if (!IsVisible(centerVS.xyz)) return;

  uint visibleIndex;
  drawCount.InterlockedAdd(0, 1, visibleIndex);

  visibleModels[visibleIndex] = model;
  visibleData[visibleIndex] = instanceData[index];
}
