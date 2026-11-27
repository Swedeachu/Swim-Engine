// AO linear depth: the view distance of every pixel at half precision, read by the AO and its
// blur in place of the depth (AoHalf bit 1). Bindings follow ScreenSpaceLinearDepthBindings.
#include "ScreenSpace/ScreenSpaceRecords.slang"

[[vk::binding(0, 0)]] Texture2D<float> Depth;
[[vk::binding(1, 0)]] StructuredBuffer<ScreenSpaceParams> Params;
[[vk::binding(2, 0)]] [[vk::image_format("r16f")]] RWTexture2D<float> Output;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	const ScreenSpaceView view = ScreenSpaceLoadView(Params);
	if (id.x >= view.Width || id.y >= view.Height)
	{
		return;
	}
	const float distance = max(ScreenSpaceLoadViewDistance(view, Depth, int(id.x), int(id.y)), 0.0);
	Output[id.xy] = Depth.Load(int3(id.xy, 0)) >= ScreenSpaceOverlayDepth ? -distance : distance;
}
