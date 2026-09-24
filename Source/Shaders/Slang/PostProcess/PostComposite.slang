// Composite (items 73-74): exposure, bloom, grading, tone mapping and output encoding
// (Post::CompositeTexel). Compiled as SwimPostComposite (rgba8 SDR output) and
// SwimPostCompositeHdr (POST_OUTPUT_HDR=1, rgba16f HDR10/scRGB output). Bindings follow
// PostCompositeBindings.
#include "PostProcess/PostProcessRecords.slang"

#ifndef POST_OUTPUT_HDR
#define POST_OUTPUT_HDR 0
#endif

struct CompositeConstants
{
	uint Width;
	uint Height;
	uint BloomWidth;
	uint BloomHeight;
};

[[vk::push_constant]] ConstantBuffer<CompositeConstants> Constants;
[[vk::binding(0, 0)]] Texture2D<float4> Source;
[[vk::binding(1, 0)]] Texture2D<float4> Bloom;
[[vk::binding(2, 0)]] StructuredBuffer<ExposureState> State;
[[vk::binding(3, 0)]] StructuredBuffer<PostParams> Params;
#if POST_OUTPUT_HDR
[[vk::binding(4, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Output;
#else
[[vk::binding(4, 0)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> Output;
#endif

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Constants.Width || id.y >= Constants.Height)
	{
		return;
	}
	const PostParams params = Params[0];
	float3 bloom = float3(0.0, 0.0, 0.0);
	if (params.BloomEnabled != 0)
	{
		bloom = PostTentUpsample(Bloom, Constants.BloomWidth, Constants.BloomHeight, id.x, id.y, Constants.Width, Constants.Height);
	}
	const float4 source = Source.Load(int3(int(id.x), int(id.y), 0));
	Output[id.xy] = PostCompositeTexel(params, State[0].Exposure, source, bloom, id.x, id.y);
}
