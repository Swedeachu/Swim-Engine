[[vk::binding(0)]] SamplerCube skybox;

struct FragmentInput
{
	float4 Position : SV_Position;
	[[vk::location(0)]] float3 TexDirection : TEXCOORD0;
};

[shader("fragment")]
float4 main(FragmentInput input) : SV_Target
{
	return skybox.Sample(normalize(input.TexDirection));
}
