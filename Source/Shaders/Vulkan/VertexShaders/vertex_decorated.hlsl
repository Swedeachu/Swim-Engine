[[vk::binding(0, 0)]]
cbuffer CameraUBO : register(b0, space0)
{
  float4x4 worldView;
  float4x4 worldProj;
  float4x4 screenView;
  float4x4 screenProj;

  float4 camParams;
  float2 viewportSize;
  float2 _padViewportSize;
};

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
  uint space;  // 0 = world, 1 = screen

  uint2 vertexOffsetInMegaBuffer;
  uint2 indexOffsetInMegaBuffer;
};

[[vk::binding(1, 0)]]
StructuredBuffer<GpuInstanceData> instanceBuffer : register(t1, space0);

struct MeshDecoratorGpuInstanceData
{
  float4 fillColor;
  float4 strokeColor;
  float2 strokeWidth;
  float2 cornerRadius;
  int enableFill;
  int enableStroke;
  int roundCorners;
  int useTexture;
  float2 resolution;
  float2 quadSize;
  int renderOnTop; // 0 = normal depth, 1 = force in front
};

[[vk::binding(2, 0)]]
StructuredBuffer<MeshDecoratorGpuInstanceData> decoratorBuffer : register(t2, space0);

struct VSInput
{
  float3 position : POSITION;  
  float3 color : COLOR;     
  float2 uv : TEXCOORD0; 
  uint   instanceID : SV_InstanceID;
};

// Matches UI pipeline layout
struct VSOutput
{
  float4 position : SV_Position;                 
  float2 uv : TEXCOORD0;                         
  nointerpolation uint textureIndex : TEXCOORD1; 
  float hasTexture : TEXCOORD2;                  
  nointerpolation uint instanceID : TEXCOORD3;   // indexing decorator buffer
  float3 color : TEXCOORD4;                      // vertex color fallback
  float2 quadSizePx : TEXCOORD5;    // (half-width, half-height) in pixels
  float2 centerPx : TEXCOORD6;    // screen-space centre of the quad
};

VSOutput main(VSInput vin)
{
  VSOutput vout;

  // Grab instance and decorator data
  GpuInstanceData inst = instanceBuffer[vin.instanceID];
  MeshDecoratorGpuInstanceData deco = decoratorBuffer[inst.materialIndex];

  // Determine transform space
  const bool isScreen = (inst.space == 1);

  // Select appropriate view/projection matrix
  float4x4 view = isScreen ? screenView : worldView;
  float4x4 proj = isScreen ? screenProj : worldProj;

  float3 worldPos;

  if (isScreen)
  {
    // In screen-space, model matrix directly gives pixel position
    float4 local = float4(vin.position, 1.0f);
    worldPos = mul(inst.model, local).xyz;
  }
  else
  {
    // In world-space, apply full model transform
    worldPos = mul(inst.model, float4(vin.position, 1.0f)).xyz;
  }

  // Final clip space position
  float4 clipPos = mul(proj, mul(view, float4(worldPos, 1.0f)));

  if (deco.renderOnTop != 0) {
    // Put at (almost) the near plane in clip space so depth ~ 0 in NDC.
    // Use a tiny epsilon to avoid clipping at exactly -w.
    clipPos.z = clipPos.w * 1e-6f;
  }
  
  vout.position = clipPos;

  // Pass-through values
  vout.uv = vin.uv;
  vout.textureIndex = inst.textureIndex;
  vout.hasTexture = inst.hasTexture;
  vout.instanceID = inst.materialIndex;
  vout.color = vin.color;

  // Pass half-size in screen-space pixels (calculated on CPU!)
  vout.quadSizePx = deco.quadSize * 0.5f;

  // Compute center of quad in screen-space for SDF math
  float4 clipCentre = mul(proj, mul(view, float4(inst.model[3].xyz, 1.0f)));
  float2 ndcCentre = clipCentre.xy / clipCentre.w;
  vout.centerPx = (ndcCentre * 0.5f + 0.5f) * viewportSize;

  return vout;
}

