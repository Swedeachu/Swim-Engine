struct ComputeConstants
{
	uint Count;
	uint Width;
	uint Height;
	uint Multiplier;
	uint Bias;
	uint Pass;
};

[[vk::push_constant]] ConstantBuffer<ComputeConstants> Parameters;
[[vk::binding(3, 1)]] StructuredBuffer<uint> Input;
[[vk::binding(7, 1)]] RWStructuredBuffer<uint> Output;

[shader("compute")]
[numthreads(8, 4, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID)
{
	uint index = tid.x + Parameters.Width * (tid.y + Parameters.Height * tid.z);
	if (index < Parameters.Count)
	{
		uint value = Input[index] * Parameters.Multiplier + Parameters.Bias;
		if (Parameters.Pass == 0)
		{
			Output[index] = value;
		}
		else
		{
			Output[index] += value;
		}
	}
}
