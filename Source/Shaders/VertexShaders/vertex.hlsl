// Uniform buffer for camera data (view and projection matrices)
cbuffer CameraUBO : register(b0)
{
    float4x4 view;
    float4x4 proj;
};

// Define push constants for the model matrix
// Note: The exact syntax may vary based on your HLSL compiler version and setup
struct PushConstants
{
    float4x4 model;
};

// Declare push constants using the appropriate attribute
[[vk::push_constant]] PushConstants pc;

struct VSInput
{
    float3 position : POSITION; // Location 0
    float3 color : COLOR;       // Location 1
};

struct VSOutput
{
    float4 position : SV_Position; // Transformed position
    float3 color : COLOR;           // Vertex color
};

VSOutput main(VSInput input)
{
    VSOutput output;

    // Apply the model, view, and projection transformations
    float4 worldPos = mul(pc.model, float4(input.position, 1.0));
    float4 viewPos = mul(view, worldPos);
    float4 projPos = mul(proj, viewPos);

    // Assign transformed vertex position
    output.position = projPos;

    // Pass through vertex color to fragment shader
    output.color = input.color;

    return output;
}
