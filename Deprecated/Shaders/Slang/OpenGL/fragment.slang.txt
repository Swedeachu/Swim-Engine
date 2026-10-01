uniform float hasTexture;
[[vk::binding(0)]] Sampler2D albedoTex;

struct FragmentInput
{
	float4 Position : SV_Position;
	[[vk::location(0)]] float3 Color : COLOR0;
	[[vk::location(1)]] float2 TexCoord : TEXCOORD0;
};

[shader("fragment")]
float4 main(FragmentInput input) : SV_Target
{
	float4 color = hasTexture > 0.5f
		? albedoTex.Sample(input.TexCoord)
		: float4(input.Color, 1.0f);

	if (color.a < 0.5f)
	{
		discard;
	}
	return color;
}
