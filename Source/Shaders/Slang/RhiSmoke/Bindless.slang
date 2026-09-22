// Bindless smoke: each invocation samples the texture/sampler pair named by its
// lookup row. Space 1 is supplied by the pipeline layout as a shared bindless
// space; the unbounded arrays reflect as runtime-sized (Count 0) bindings.
struct DispatchSettings
{
	uint Count;
	uint Reserved0;
	uint Reserved1;
	uint Reserved2;
};

struct Lookup
{
	uint Texture;
	uint Sampler;
};

[[vk::push_constant]] ConstantBuffer<DispatchSettings> Dispatch;
[[vk::binding(0, 0)]] StructuredBuffer<Lookup> Lookups;
[[vk::binding(1, 0)]] RWStructuredBuffer<float4> Results;
[[vk::binding(0, 1)]] SamplerState Samplers[];
[[vk::binding(1, 1)]] Texture2D<float4> Textures[];

[shader("compute")]
[numthreads(8, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Dispatch.Count)
	{
		return;
	}
	const Lookup lookup = Lookups[id.x];
	// Indices differ per invocation, so both arrays need nonuniform indexing. The
	// coordinate sits on the texel edge of a 2x1 texture, so nearest and linear
	// samplers return different colors.
	Results[id.x] = Textures[NonUniformResourceIndex(lookup.Texture)].SampleLevel(
		Samplers[NonUniformResourceIndex(lookup.Sampler)], float2(0.5, 0.5), 0);
}
