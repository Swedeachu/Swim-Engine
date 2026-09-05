// Sparse descriptor spaces exercise reflection and empty set-layout gaps.
[[vk::binding(3, 2)]] Texture2D<float4> sourceTexture;
[[vk::binding(7, 2)]] SamplerState sourceSampler;

struct VertexOutput
{
	float4 Position : SV_Position;
	float2 UV : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID)
{
	static const float2 positions[3] = { float2(-1, -1), float2(3, -1), float2(-1, 3) };
	VertexOutput output;
	output.Position = float4(positions[vertexId], 0.5, 1);
	output.UV = float2((positions[vertexId].x + 1) * 0.5, (1 - positions[vertexId].y) * 0.5);
	return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
	return sourceTexture.Sample(sourceSampler, input.UV);
}
