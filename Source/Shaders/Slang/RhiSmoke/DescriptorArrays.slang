struct Settings
{
	uint Multiply;
	uint Add;
	uint Reserved0;
	uint Reserved1;
};

struct DispatchSettings
{
	uint Count;
	uint Seed;
	uint Pass;
	uint Reserved;
};

[[vk::push_constant]] ConstantBuffer<DispatchSettings> Dispatch;
[[vk::binding(2, 0)]] StructuredBuffer<uint> Inputs[2];
[[vk::binding(3, 0)]] ConstantBuffer<Settings> Parameters[2];
[[vk::binding(4, 0)]] Texture2D<float4> Images[2];
[[vk::binding(5, 0)]] SamplerState Samplers[2];
[[vk::binding(7, 1)]] [[vk::image_format("r32ui")]] RWTexture2D<uint> Outputs[2];

[shader("compute")]
[numthreads(8, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID,
	[[vk::binding(9, 1)]] uniform RWStructuredBuffer<uint> Buffers[2])
{
	if (id.x >= Dispatch.Count)
	{
		return;
	}
	// Descriptor indices are compile-time constants. Dynamic/non-uniform
	// indexing features are a separate contract from fixed array allocation.
	if (Dispatch.Pass == 0)
	{
		uint first = Inputs[0][id.x] * Parameters[0].Multiply + Parameters[0].Add + Dispatch.Seed;
		uint second = Inputs[1][id.x] * Parameters[1].Multiply + Parameters[1].Add + Dispatch.Seed;
		first += uint(Images[0].SampleLevel(Samplers[0], float2(0.5, 0.5), 0).x) * 17;
		second += uint(Images[1].SampleLevel(Samplers[1], float2(0.5, 0.5), 0).x) * 17;
		Buffers[0][id.x] = first;
		Buffers[1][id.x] = second;
		Outputs[0][id.xy] = first + 101;
		Outputs[1][id.xy] = second + 101;
	}
	else
	{
		Buffers[0][id.x] += 11;
		Buffers[1][id.x] += 11;
		Outputs[0][id.xy] += 13;
		Outputs[1][id.xy] += 13;
	}
}
