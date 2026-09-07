struct VertexInput
{
	[[vk::location(0)]] float2 Position : POSITION0;
	[[vk::location(1)]] float4 Color : COLOR0;
	[[vk::location(2)]] float2 InstanceOffset : TEXCOORD0;
	[[vk::location(3)]] float4 InstanceColor : COLOR1;
};

struct VertexOutput
{
	float4 Position : SV_Position;
	float4 Color : COLOR0;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
	VertexOutput output;
	output.Position = float4(input.Position + input.InstanceOffset, 0.5, 1.0);
	output.Color = input.Color * input.InstanceColor;
	return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
	return input.Color;
}
