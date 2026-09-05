struct VertexOutput
{
	float4 Position : SV_Position;
};

[shader("vertex")]
VertexOutput main(uint vertexId : SV_VertexID)
{
	VertexOutput output;
	float2 position;
	if (vertexId == 0)
	{
		position = float2(-1.0f, -1.0f);
	}
	else if (vertexId == 1)
	{
		position = float2(3.0f, -1.0f);
	}
	else
	{
		position = float2(-1.0f, 3.0f);
	}
	output.Position = float4(position, 0.0f, 1.0f);
	return output;
}
