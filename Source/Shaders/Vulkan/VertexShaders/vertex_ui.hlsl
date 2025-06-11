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

struct UIParams
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
};

[[vk::binding(2, 0)]]
StructuredBuffer<UIParams> uiParamBuffer : register(t2, space0);

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
  nointerpolation uint instanceID : TEXCOORD3;   // indexing UIParams
  float3 color : TEXCOORD4;                      // vertex color fallback
  float2 quadSizePx : TEXCOORD5;    // (half-width, half-height) in pixels
  float2 centerPx : TEXCOORD6;    // screen-space centre of the quad
};

VSOutput main(VSInput vin)
{
  VSOutput vout;

  GpuInstanceData inst = instanceBuffer[vin.instanceID];
  UIParams ui = uiParamBuffer[inst.materialIndex];

  const bool isScreen = (inst.space == 1);

  float4x4 view = isScreen ? screenView : worldView;
  float4x4 proj = isScreen ? screenProj : worldProj;

  float3 worldPos;

  if (isScreen)
  {
    float4 local = float4(vin.position, 1.0f);
    worldPos = mul(inst.model, local).xyz;
  }
  else
  {
    // regular model-space -> world-space transform 
    worldPos = mul(inst.model, float4(vin.position, 1.0f)).xyz;

    // we still need the quad’s projected half-size in pixels for the SDF
    float3 centreWS = inst.model[3].xyz;
    float3 centreVS = mul(worldView, float4(centreWS, 1.0f)).xyz;
    float  absZ = abs(centreVS.z);

    float worldPerPxX = (2.0f * absZ * camParams.x) / viewportSize.x;
    float worldPerPxY = (2.0f * absZ * camParams.y) / viewportSize.y;

    // local half-size in world units (scale is |column| * 0.5)
    float halfWorldX = length(inst.model[0].xyz) * 0.5f;
    float halfWorldY = length(inst.model[1].xyz) * 0.5f;

    vout.quadSizePx = float2(
      halfWorldX / worldPerPxX,
      halfWorldY / worldPerPxY
    );
  }

  float4 clipPos = mul(proj, mul(view, float4(worldPos, 1.0f)));
  vout.position = clipPos;
  vout.uv = vin.uv;
  vout.textureIndex = inst.textureIndex;
  vout.hasTexture = inst.hasTexture;
  vout.instanceID = inst.materialIndex;
  vout.color = vin.color;

  vout.quadSizePx = ui.quadSize * 0.5f;

  float2 ndc = clipPos.xy / clipPos.w;
  vout.centerPx = (ndc * 0.5f + 0.5f) * viewportSize;

  return vout;
}
