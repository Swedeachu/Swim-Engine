// Luminance histogram of HDR scene color (item 73): Post::BuildHistogram. Each 16x16
// group bins into group-shared counters, then adds its non-zero bins to the global
// histogram (cleared before the pass). Bindings follow PostHistogramBindings.
#include "PostProcess/PostProcessRecords.slang"

struct HistogramConstants
{
	uint Width;
	uint Height;
	float MinLog2Luminance;
	float InverseLog2Range;
};

[[vk::push_constant]] ConstantBuffer<HistogramConstants> Constants;
[[vk::binding(0, 0)]] Texture2D<float4> Source;
[[vk::binding(1, 0)]] RWStructuredBuffer<uint> Histogram;

groupshared uint LocalBins[PostHistogramBins];

[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(uint3 id : SV_DispatchThreadID, uint index : SV_GroupIndex)
{
	LocalBins[index] = 0;
	GroupMemoryBarrierWithGroupSync();
	if (id.x < Constants.Width && id.y < Constants.Height)
	{
		const float3 color = Source.Load(int3(int(id.x), int(id.y), 0)).xyz;
		const uint bin = PostHistogramBin(PostLuminance(color), Constants.MinLog2Luminance, Constants.InverseLog2Range);
		InterlockedAdd(LocalBins[bin], 1u);
	}
	GroupMemoryBarrierWithGroupSync();
	const uint count = LocalBins[index];
	if (count != 0)
	{
		InterlockedAdd(Histogram[index], count);
	}
}
