uniform float4x4 mvp;
uniform float2 pxToModel;
uniform float emScalePx;
uniform int isWorldSpace;

struct VertexInput
{
	[[vk::location(0)]] float2 PositionEm : POSITION;
	[[vk::location(1)]] float2 TexCoord : TEXCOORD0;
};

struct VertexOutput
{
	float4 Position : SV_Position;
	[[vk::location(0)]] float2 TexCoord : TEXCOORD0;
};

[shader("vertex")]
VertexOutput main(VertexInput input)
{
	VertexOutput output;
	float2 positionPixels = input.PositionEm * emScalePx;
	float2 positionModel = positionPixels * pxToModel;
	output.TexCoord = input.TexCoord;
	output.Position = mul(mvp, float4(positionModel, 0.0f, 1.0f));
	return output;
}
