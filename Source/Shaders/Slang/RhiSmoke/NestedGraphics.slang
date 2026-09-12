struct Resources
{
	Texture2D<float4> Color;
	SamplerState Linear;
};

struct MaterialParameters
{
	float4 Tint;
	Resources Textures;
};

[[vk::binding(3, 1)]] Resources Shared;

[shader("vertex")]
float4 vertexMain(uint id : SV_VertexID) : SV_Position
{
	float2 positions[3] = { float2(-1, -1), float2(3, -1), float2(-1, 3) };
	return float4(positions[id], 0, 1);
}

[shader("fragment")]
float4 fragmentMain([[vk::binding(0, 4)]] uniform ParameterBlock<MaterialParameters> Material) : SV_Target0
{
	return Shared.Color.SampleLevel(Shared.Linear, float2(0.5, 0.5), 0) * Material.Tint *
		Material.Textures.Color.SampleLevel(Material.Textures.Linear, float2(0.5, 0.5), 0);
}
