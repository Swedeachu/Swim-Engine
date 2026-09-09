[[vk::binding(0, 0)]] Texture2D<float> Depth;
[[vk::binding(1, 0)]] Texture2D<float> PackedDepth;
[[vk::binding(2, 0)]] SamplerComparisonState Comparisons[2];
[[vk::binding(3, 0)]] SamplerState PointSampler;
[[vk::binding(4, 0)]] RWStructuredBuffer<float4> Output;

[shader("compute")]
[numthreads(4, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= 6)
	{
		return;
	}
	float reference = float(id.x) * 0.2;
	Output[id.x * 2] = float4(Depth.Load(int3(0, 0, 0)),
		Depth.SampleLevel(PointSampler, float2(0.5, 0.5), 0),
		Depth.SampleCmpLevelZero(Comparisons[0], float2(0.5, 0.5), reference),
		Depth.SampleCmpLevelZero(Comparisons[1], float2(0.5, 0.5), reference));
	Output[id.x * 2 + 1] = float4(PackedDepth.Load(int3(0, 0, 0)),
		PackedDepth.SampleLevel(PointSampler, float2(0.5, 0.5), 0),
		PackedDepth.SampleCmpLevelZero(Comparisons[0], float2(0.5, 0.5), reference),
		PackedDepth.SampleCmpLevelZero(Comparisons[1], float2(0.5, 0.5), reference));
}
