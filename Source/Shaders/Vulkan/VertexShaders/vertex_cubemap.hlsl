struct PushConstants
{
  float4x4 view;
  float4x4 proj;
};

[[vk::push_constant]]
PushConstants pc;

struct VSInput
{
  float3 position : POSITION;
};

struct VSOutput
{
  float4 position : SV_Position;
  float3 texDir : TEXCOORD0;
};

VSOutput main(VSInput input)
{
  VSOutput output;

  // Sample direction
  output.texDir = input.position;

  float4 clipPos = mul(pc.proj, mul(pc.view, float4(input.position, 1.0f)));

  // Keep skybox at far depth
  output.position = clipPos.xyww;

  return output;
}
