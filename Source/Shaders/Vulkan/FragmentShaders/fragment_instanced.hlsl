SamplerState texSampler : register(s0, space1);
Texture2D    textures[] : register(t1, space1); // Bindless texture array

struct FSInput
{
  float4 position : SV_Position;
  float3 color : COLOR;
  float2 uv : TEXCOORD0;
  uint   textureIndex : TEXCOORD1;
  float  hasTexture : TEXCOORD2;
};

float4 main(FSInput input) : SV_Target
{
  if (input.hasTexture > 0.5f)
  {
    return textures[input.textureIndex].Sample(texSampler, input.uv);
  }
  else
  {
    return float4(input.color, 1.0f);
  }
}
