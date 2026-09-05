struct VertexOutput
{
	float4 Position : SV_Position;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID)
{
	static const float2 positions[3] =
	{
		float2(-0.75, -0.75),
		float2(0.75, -0.75),
		float2(0.0, 0.75)
	};
	VertexOutput output;
	output.Position = float4(positions[vertexId], 0.5, 1.0);
	return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
	return float4(0.0, 1.0, 0.0, 1.0);
}
