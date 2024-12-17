
struct PSInput
{
    float4 position : SV_Position; // Transformed position (not used)
    float3 color : COLOR;           // Vertex color
};

struct PSOutput
{
    float4 outColor : SV_Target;    // Final pixel color
};

PSOutput main(PSInput input)
{
    PSOutput output;
    output.outColor = float4(input.color, 1.0);
    return output;
}
