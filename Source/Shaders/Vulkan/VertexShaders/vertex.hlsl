cbuffer CameraUBO : register(b0)
{
    float4x4 view;
    float4x4 proj;
};

// We'll read the push constant, though we only use model inside the vertex shader
struct PushConstantData
{
    float4x4 model;
    float hasTexture;
    float padA;
    float padB;
    float padC;
};

[[vk::push_constant]]
PushConstantData pc;

struct VSInput
{
    float3 position : POSITION; // location=0
    float3 color : COLOR; // location=1
    float2 uv : TEXCOORD0; // location=2
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 color : COLOR;
    float2 uv : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    float4 worldPos = mul(pc.model, float4(input.position, 1.0f));
    float4 viewPos = mul(view, worldPos);
    float4 projPos = mul(proj, viewPos);

    output.position = projPos;
    output.color = input.color;
    output.uv = input.uv;
    return output;
}
