// Bloom upsample (item 74): Post::BloomUpsample, Destination = High + tent(Low).
// Bindings follow PostBloomUpsampleBindings.
#include "PostProcess/PostProcessRecords.slang"

struct UpsampleConstants
{
	uint LowWidth;
	uint LowHeight;
	uint DestinationWidth;
	uint DestinationHeight;
};

[[vk::push_constant]] ConstantBuffer<UpsampleConstants> Constants;
[[vk::binding(0, 0)]] Texture2D<float4> Low;
[[vk::binding(1, 0)]] Texture2D<float4> High;
[[vk::binding(2, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Destination;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Constants.DestinationWidth || id.y >= Constants.DestinationHeight)
	{
		return;
	}
	const float3 tent = PostTentUpsample(
		Low, Constants.LowWidth, Constants.LowHeight, id.x, id.y, Constants.DestinationWidth, Constants.DestinationHeight);
	Destination[id.xy] = float4(High.Load(int3(int(id.x), int(id.y), 0)).xyz + tent, 1.0);
}
