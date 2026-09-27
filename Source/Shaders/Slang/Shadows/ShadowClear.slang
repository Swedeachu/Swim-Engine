// Clears one shadow-atlas tile to the far depth (reverse-Z 0) with a full-viewport triangle
// (the viewport and scissor are the tile), for the cascade cache: tiles that keep last
// frame's depth are loaded, only the tiles re-rendered this frame are cleared first
// (ShadowFrame::Atlas). Depth test Always, depth write on, no colour.
struct VertexOutput
{
	float4 Position : SV_Position;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VulkanVertexID)
{
	static const float2 positions[3] = { float2(-1, -1), float2(3, -1), float2(-1, 3) };
	VertexOutput output;
	output.Position = float4(positions[vertexId], 0.0, 1.0);
	return output;
}

[shader("fragment")]
void fragmentMain()
{
}
