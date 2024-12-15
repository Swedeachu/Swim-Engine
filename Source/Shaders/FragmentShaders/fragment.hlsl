struct PSInput
{
    float4 position : SV_Position; // Maps to location 0 in Vulkan
    float3 color : COLOR; // Maps to location 1 in Vulkan
};

float4 main(PSInput input) : SV_Target
{
    return float4(input.color, 1.0);
}
