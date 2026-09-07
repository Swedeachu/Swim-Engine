struct DrawConstants
{
	float2 Offset;
	float2 Scale;
	float4 Color;
};

[[vk::push_constant]] ConstantBuffer<DrawConstants> Draw;

[shader("vertex")]
float4 vertexMain(uint vertexId : SV_VertexID) : SV_Position
{
	const float2 positions[3] = { float2(-1.0, -1.0), float2(1.0, -1.0), float2(0.0, 1.0) };
	return float4(positions[vertexId] * Draw.Scale + Draw.Offset, 0.5, 1.0);
}

[shader("fragment")]
float4 fragmentMain() : SV_Target0
{
	return Draw.Color;
}
