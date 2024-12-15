// Must match C++ code for CameraUBO
cbuffer CameraUBO : register(b0)
{
    float4x4 view;
    float4x4 proj;
    float4x4 model;
};

struct VSInput
{
    float3 position : POSITION;
    float3 color : COLOR;
};

struct VSOutput
{
    float4 position : SV_Position; // Will map to location 0 in Vulkan
    float3 color : COLOR; // Will map to location 1 in Vulkan
};

VSOutput main(VSInput input)
{
    VSOutput output;

    // Apply the model, view, and projection transformations
    float4 worldPos = mul(model, float4(input.position, 1.0));
    float4 viewPos = mul(view, worldPos);
    float4 projPos = mul(proj, viewPos);

    // Assign transformed vertex position
    output.position = projPos;

    // Pass through vertex color to fragment stage
    output.color = input.color;

    return output;
}
