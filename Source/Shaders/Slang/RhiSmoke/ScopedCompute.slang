struct DispatchSettings
{
	uint Count;
	uint Scale;
	uint Seed;
	uint Pass;
};

[[vk::push_constant]] ConstantBuffer<DispatchSettings> Dispatch;
[[vk::binding(2, 0)]] StructuredBuffer<uint> Input;

[shader("compute")]
[numthreads(8, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID,
	[[vk::binding(5, 1)]] uniform RWStructuredBuffer<uint> Output,
	[[vk::binding(9, 1)]] uniform ConstantBuffer<DispatchSettings> Parameters)
{
	if (id.x >= Dispatch.Count)
	{
		return;
	}
	if (Dispatch.Pass == 0)
	{
		Output[id.x] = Input[id.x] * Parameters.Scale + Parameters.Seed;
	}
	else
	{
		Output[id.x] += Dispatch.Seed;
	}
}
