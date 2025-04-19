// We'll read from set=0, binding=1 = t1/s1 for the combined sampler
Texture2D albedoTex : register(t1);
SamplerState albedoSampler : register(s1);

struct PushConstantData
{
    float4x4 model; // not really needed in fragment, but it’s part of the same struct
    float hasTexture;
    float padA;
    float padB;
    float padC;
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
    // If hasTexture > 0.5, sample from albedoTex (maybe we can use hasTexture as an alpha value)
    if (pc.hasTexture > 0.5f)
    {
        return albedoTex.Sample(albedoSampler, input.uv);
    }
    else
    {
        // Just output the interpolated vertex color
        return float4(input.color, 1.0f);
    }
}
