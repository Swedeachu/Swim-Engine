struct VertexOutput
{
	float4 Position : SV_Position;
	float4 Color : COLOR0;
};

struct ColorSettings
{
	float4 Color;
};

[[vk::binding(2, 0)]] ConstantBuffer<ColorSettings> Shared;

[shader("vertex")]
VertexOutput vertexMain(uint id : SV_VertexID,
	[[vk::binding(3, 1)]] uniform ConstantBuffer<ColorSettings> VertexData)
{
	VertexOutput output;
	output.Position = float4(float(id), 0.0, 0.0, 1.0);
	output.Color = Shared.Color * VertexData.Color;
	return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input,
	[[vk::binding(7, 1)]] uniform Texture2D<float4> Image,
	[[vk::binding(9, 1)]] uniform SamplerState ImageSampler) : SV_Target0
{
	return input.Color * Shared.Color * Image.SampleLevel(ImageSampler, float2(0.5, 0.5), 0.0);
}
