uniform float4x4 projection;
uniform float4x4 view;
uniform float3x3 rotation;

struct VertexInput
{
	[[vk::location(0)]] float3 Position : POSITION;
};

struct VertexOutput
{
	float4 Position : SV_Position;
	[[vk::location(0)]] float3 TexDirection : TEXCOORD0;
};

[shader("vertex")]
VertexOutput main(VertexInput input)
{
	VertexOutput output;
	output.TexDirection = mul(rotation, input.Position);
	float4 position = mul(projection, mul(view, float4(input.Position, 1.0f)));
	output.Position = position.xyww;
	return output;
}
