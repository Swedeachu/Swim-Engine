// TAA resolve (critical-path item 75): Temporal::ResolveTexel for every texel.
// Bindings follow TemporalResolveBindings.
#include "Temporal/TemporalRecords.slang"

[[vk::push_constant]] ConstantBuffer<TemporalResolveConstants> Constants;
[[vk::binding(0, 0)]] Texture2D<float4> Current;
[[vk::binding(1, 0)]] Texture2D<float> Depth;
[[vk::binding(2, 0)]] Texture2D<float2> Velocity;
[[vk::binding(3, 0)]] Texture2D<float4> History;
[[vk::binding(4, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Output;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	const uint width = Constants.Width;
	const uint height = Constants.Height;
	if (id.x >= width || id.y >= height)
	{
		return;
	}
	// Temporal::Gather: 3 x 3, clamped, row by row; the first nearest texel wins ties.
	float3 sum = float3(0.0, 0.0, 0.0);
	float3 sumSquares = float3(0.0, 0.0, 0.0);
	float3 minimum = float3(1.0e30, 1.0e30, 1.0e30);
	float3 maximum = float3(-1.0e30, -1.0e30, -1.0e30);
	float nearest = -1.0;
	float2 velocity = float2(0.0, 0.0);
	float3 center = float3(0.0, 0.0, 0.0);
	for (int dy = -1; dy <= 1; ++dy)
	{
		for (int dx = -1; dx <= 1; ++dx)
		{
			const int3 at = int3(clamp(int(id.x) + dx, 0, int(width) - 1), clamp(int(id.y) + dy, 0, int(height) - 1), 0);
			const float3 rgb = TemporalSanitize(Current.Load(at));
			const float3 ycocg = TemporalRgbToYCoCg(rgb);
			sum += ycocg;
			sumSquares += ycocg * ycocg;
			minimum = min(minimum, ycocg);
			maximum = max(maximum, ycocg);
			const float d = Depth.Load(at);
			if (d > nearest) // Reverse-Z: larger is nearer.
			{
				nearest = d;
				velocity = Velocity.Load(at);
			}
			if (dx == 0 && dy == 0)
			{
				center = rgb;
			}
		}
	}
	const float3 mean = sum / 9.0;
	const float3 sigma = sqrt(max(sumSquares / 9.0 - mean * mean, float3(0.0, 0.0, 0.0)));
	const float3 low = max(mean - Constants.ClipGamma * sigma, minimum);
	const float3 high = min(mean + Constants.ClipGamma * sigma, maximum);

	// Temporal::ResolveTexel.
	const float2 uv = float2((float(id.x) + 0.5) / float(width), (float(id.y) + 0.5) / float(height));
	const float2 previous = uv - velocity;
	const bool onScreen = previous.x >= 0.0 && previous.x <= 1.0 && previous.y >= 0.0 && previous.y <= 1.0;
	if (Constants.HistoryValid == 0u || !onScreen)
	{
		Output[id.xy] = float4(center, 1.0);
		return;
	}
	const float4 sampled = TemporalSampleBilinear(History, width, height, previous);
	const float3 clipped = TemporalYCoCgToRgb(TemporalClipToBox(TemporalRgbToYCoCg(TemporalSanitize(sampled)), low, high));
	const float3 past = max(clipped, float3(0.0, 0.0, 0.0));
	const float currentWeight = Constants.Feedback / (1.0 + TemporalLuminance(center));
	const float pastWeight = (1.0 - Constants.Feedback) / (1.0 + TemporalLuminance(past));
	const float inverseTotal = 1.0 / (currentWeight + pastWeight);
	Output[id.xy] = float4((center * currentWeight + past * pastWeight) * inverseTotal, 1.0);
}
