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

// Half-resolution AO: the 3 x 3 traced texels around the pixel (a 6 x 6 pixel footprint),
// each weighted by how close the depth of the pixel it traced is to this pixel's.
float HalfBlur(ScreenSpaceView view, int x, int y)
{
	const int2 halfSize = int2((int(view.Width) + 1) / 2, (int(view.Height) + 1) / 2);
	const int2 centre = int2(x, y) / 2;
	const int2 offset = int2(ScreenSpaceHalfOffset(Params[0].NoiseFrame));
	bool valid;
	const float3 position = ScreenSpaceLoadViewPosition(view, Depth, x, y, valid);
	if (!valid)
	{
		return Source.Load(int3(centre, 0));
	}
	const float centerDepth = -position.z;
	const float tolerance = Params[0].AoBlurDepthTolerance * abs(centerDepth);
	float sum = 0.0;
	float weights = 0.0;
	for (int dy = -1; dy <= 1; ++dy)
	{
		for (int dx = -1; dx <= 1; ++dx)
		{
			const int2 texel = centre + int2(dx, dy);
			if (any(texel < 0) || any(texel >= halfSize))
			{
				continue;
			}
			const int2 source = min(texel * 2 + offset, int2(int(view.Width), int(view.Height)) - 1);
			bool sampleValid;
			const float3 samplePosition = ScreenSpaceLoadViewPosition(view, Depth, source.x, source.y, sampleValid);
			if (!sampleValid)
			{
				continue;
			}
			const float weight = max(0.0, 1.0 - abs(-samplePosition.z - centerDepth) / tolerance);
			sum += weight * Source.Load(int3(texel, 0));
			weights += weight;
		}
	}
	return weights > 0.0 ? sum / weights : Source.Load(int3(centre, 0));
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
	if (Params[0].AoHalf != 0u)
	{
		Output[id.xy] = Finish(HalfBlur(view, x, y), power);
		return;
	}
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
