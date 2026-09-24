// AO blur (item 76): ScreenSpace::BlurTexel for every pixel. Bindings follow ScreenSpaceBlurBindings.
#include "ScreenSpace/ScreenSpaceRecords.slang"

[[vk::binding(0, 0)]] Texture2D<float> Source;
[[vk::binding(1, 0)]] Texture2D<float> Depth;
[[vk::binding(2, 0)]] StructuredBuffer<ScreenSpaceParams> Params;
[[vk::binding(3, 0)]] [[vk::image_format("r32f")]] RWTexture2D<float> Output;

float Finish(float value, float power)
{
	return pow(clamp(value, 0.0, 1.0), power);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	const ScreenSpaceView view = ScreenSpaceLoadView(Params);
	if (id.x >= view.Width || id.y >= view.Height)
	{
		return;
	}
	const float power = Params[0].AoPower;
	const int x = int(id.x);
	const int y = int(id.y);
	bool valid;
	const float3 center = ScreenSpaceLoadViewPosition(view, Depth, x, y, valid);
	if (!valid)
	{
		Output[id.xy] = Finish(Source.Load(int3(x, y, 0)), power);
		return;
	}
	const float centerDepth = -center.z;
	const float tolerance = Params[0].AoBlurDepthTolerance * abs(centerDepth);
	float sum = 0.0;
	float weights = 0.0;
	for (int dy = -2; dy <= 2; ++dy)
	{
		for (int dx = -2; dx <= 2; ++dx)
		{
			const int sx = x + dx;
			const int sy = y + dy;
			if (sx < 0 || sy < 0 || sx >= int(view.Width) || sy >= int(view.Height))
			{
				continue;
			}
			bool sampleValid;
			const float3 position = ScreenSpaceLoadViewPosition(view, Depth, sx, sy, sampleValid);
			if (!sampleValid)
			{
				continue;
			}
			const float weight = max(0.0, 1.0 - abs(-position.z - centerDepth) / tolerance);
			sum += weight * Source.Load(int3(sx, sy, 0));
			weights += weight;
		}
	}
	Output[id.xy] = Finish(weights > 0.0 ? sum / weights : Source.Load(int3(x, y, 0)), power);
}
