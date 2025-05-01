SamplerState texSampler : register(s0, space1);         // Binding 0: Sampler
Texture2D     textures[] : register(t1, space1);        // Binding 1: Bindless texture array

struct PushConstantData
{
  float4x4 model;     
  uint textureIndex;
  float hasTexture;
  float padA;
  float padB;
};

[[vk::push_constant]]
PushConstantData pc;

struct FSInput
{
  float4 position : SV_Position;
  float3 color : COLOR;
  float2 uv : TEXCOORD0;
};

float4 main(FSInput input) : SV_Target
{
    if (pc.hasTexture > 0.5f)
    {
        return textures[pc.textureIndex].Sample(texSampler, input.uv);
    }
    else
    {
        return float4(input.color, 1.0f);
    }
}
