// Runtime presentation (Phase 23): copies the finished frame (post-processed and with
// the UI composited, sRGB-encoded RGBA8) onto a swapchain image with one fullscreen
// triangle. When the swapchain format is itself *Srgb the hardware encodes on write, so
// the source is decoded to linear first and the result is bit-exact either way.
[[vk::binding(0, 0)]] Texture2D<float4> Source;
[[vk::binding(1, 0)]] SamplerState SourceSampler;

struct PresentConstants
{
	uint DecodeSrgb; // 1: the target format is *Srgb.
	uint Reserved0;
	uint Reserved1;
	uint Reserved2;
};

[[vk::push_constant]] ConstantBuffer<PresentConstants> Constants;

struct VertexOutput
{
	float4 Position : SV_Position;
	float2 Uv : TEXCOORD0;
};

// gl_VertexIndex directly (SV_VulkanVertexID): the fullscreen triangle is drawn from
// vertex 0, so it equals SV_VertexID without the DrawParameters capability.
[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VulkanVertexID)
{
	static const float2 positions[3] = { float2(-1, -1), float2(3, -1), float2(-1, 3) };
	VertexOutput output;
	output.Position = float4(positions[vertexId], 0.0, 1.0);
	output.Uv = float2((positions[vertexId].x + 1) * 0.5, (1 - positions[vertexId].y) * 0.5);
	return output;
}

float SrgbToLinear(float value)
{
	return value <= 0.04045 ? value / 12.92 : pow((value + 0.055) / 1.055, 2.4);
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
	const float4 color = Source.SampleLevel(SourceSampler, input.Uv, 0.0);
	if (Constants.DecodeSrgb != 0u)
	{
		return float4(SrgbToLinear(color.r), SrgbToLinear(color.g), SrgbToLinear(color.b), color.a);
	}
	return color;
}
