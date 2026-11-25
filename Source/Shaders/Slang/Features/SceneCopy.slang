// The opaque scene copied before the transparent pass (RenderFeatureDrawContext::SceneColor
// and SceneDepth), for transparent surfaces that look through themselves.
[[vk::binding(0, 0)]] Texture2D<float4> Color;
[[vk::binding(1, 0)]] Texture2D<float> Depth;
[[vk::binding(2, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> ColorCopy;
[[vk::binding(3, 0)]] [[vk::image_format("r32f")]] RWTexture2D<float> DepthCopy;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	uint width, height;
	Color.GetDimensions(width, height);
	if (id.x >= width || id.y >= height)
	{
		return;
	}
	ColorCopy[id.xy] = float4(Color.Load(int3(id.xy, 0)).rgb, 1.0);
	DepthCopy[id.xy] = Depth.Load(int3(id.xy, 0));
}
