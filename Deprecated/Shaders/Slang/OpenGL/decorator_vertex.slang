uniform float4x4 mvp;
uniform int renderOnTop;

struct VertexInput
{
	[[vk::location(0)]] float3 Position : POSITION;
	[[vk::location(1)]] float3 Color : COLOR0;
	[[vk::location(2)]] float2 TexCoord : TEXCOORD0;
};

struct VertexOutput
{
	float4 Position : SV_Position;
	[[vk::location(0)]] noperspective float2 TexCoord : TEXCOORD0;
	[[vk::location(1)]] float3 Color : COLOR0;
};

[shader("vertex")]
VertexOutput main(VertexInput input)
{
	VertexOutput output;
	output.TexCoord = input.TexCoord;
	output.Color = input.Color;
	output.Position = mul(mvp, float4(input.Position, 1.0f));

	if (renderOnTop > 0)
	{
		const int maxLayers = 100;
		const float nearEpsilon = 1.0e-6f;
		const float nearForced = -1.0f + nearEpsilon;
		const float farForced = -0.8f;
		const float depthRange = farForced - nearForced;

		int layer = min(renderOnTop, maxLayers);
		float t = float(maxLayers - layer) / float(maxLayers);
		float forcedDepth = nearForced + t * depthRange;
		output.Position.z = output.Position.w * forcedDepth;
	}

	return output;
}
