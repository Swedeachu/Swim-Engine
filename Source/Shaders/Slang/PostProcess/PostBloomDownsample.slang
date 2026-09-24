// Bloom downsample (item 74): Post::BloomDownsample. 13 taps, each an exact 2x2 box
// average around a texel corner (Jimenez 2014); the first level multiplies by the
// exposure, uses the Karis average and applies the soft-knee threshold. Bindings follow
// PostBloomDownsampleBindings.
#include "PostProcess/PostProcessRecords.slang"

struct DownsampleConstants
{
	uint SourceWidth;
	uint SourceHeight;
	uint DestinationWidth;
	uint DestinationHeight;
	uint First;
	float Threshold;
	float Knee;
	uint Reserved;
};

[[vk::push_constant]] ConstantBuffer<DownsampleConstants> Constants;
[[vk::binding(0, 0)]] Texture2D<float4> Source;
[[vk::binding(1, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Destination;
[[vk::binding(2, 0)]] StructuredBuffer<ExposureState> State;

float3 Fetch(int x, int y)
{
	const int cx = clamp(x, 0, int(Constants.SourceWidth) - 1);
	const int cy = clamp(y, 0, int(Constants.SourceHeight) - 1);
	return Source.Load(int3(cx, cy, 0)).xyz;
}

// The 2x2 texels around corner (cx, cy): texels cx - 1 .. cx.
float3 Box(int cx, int cy)
{
	return (Fetch(cx - 1, cy - 1) + Fetch(cx, cy - 1) + Fetch(cx - 1, cy) + Fetch(cx, cy)) * 0.25;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Constants.DestinationWidth || id.y >= Constants.DestinationHeight)
	{
		return;
	}
	const int cx = int(2 * id.x + 1);
	const int cy = int(2 * id.y + 1);
	const float3 a = Box(cx - 2, cy - 2);
	const float3 b = Box(cx, cy - 2);
	const float3 c = Box(cx + 2, cy - 2);
	const float3 d = Box(cx - 1, cy - 1);
	const float3 e = Box(cx + 1, cy - 1);
	const float3 f = Box(cx - 2, cy);
	const float3 g = Box(cx, cy);
	const float3 h = Box(cx + 2, cy);
	const float3 i = Box(cx - 1, cy + 1);
	const float3 j = Box(cx + 1, cy + 1);
	const float3 k = Box(cx - 2, cy + 2);
	const float3 l = Box(cx, cy + 2);
	const float3 m = Box(cx + 2, cy + 2);
	float3 groups[5] = { (d + e + i + j) * 0.25, (a + b + f + g) * 0.25, (b + c + g + h) * 0.25, (f + g + k + l) * 0.25,
		(g + h + l + m) * 0.25 };
	const float weights[5] = { 0.5, 0.125, 0.125, 0.125, 0.125 };
	float3 sum = float3(0.0, 0.0, 0.0);
	if (Constants.First != 0)
	{
		const float exposure = State[0].Exposure;
		float total = 0.0;
		for (int n = 0; n < 5; ++n)
		{
			groups[n] *= exposure;
			const float w = weights[n] / (1.0 + PostLuminance(groups[n]));
			total += w;
			sum += groups[n] * w;
		}
		sum /= total;
		sum = PostBloomThreshold(sum, Constants.Threshold, Constants.Knee);
	}
	else
	{
		for (int n = 0; n < 5; ++n)
		{
			sum += groups[n] * weights[n];
		}
	}
	Destination[id.xy] = float4(sum, 1.0);
}
