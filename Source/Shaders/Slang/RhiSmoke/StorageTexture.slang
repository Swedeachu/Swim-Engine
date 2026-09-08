struct StorageParameters
{
	uint Width;
	uint Height;
	uint ActiveWidth;
	uint ActiveHeight;
	uint Seed;
	uint Pass;
};

[[vk::push_constant]] ConstantBuffer<StorageParameters> Parameters;
[[vk::binding(3, 1)]] [[vk::image_format("rgba32f")]] RWTexture2D<float4> FloatOutput;
[[vk::binding(7, 1)]] [[vk::image_format("r32ui")]] RWTexture2D<uint> UintOutput;
[[vk::binding(11, 1)]] [[vk::image_format("r32i")]] RWTexture2D<int> IntOutput;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID)
{
	if (tid.x >= Parameters.Width || tid.y >= Parameters.Height ||
		tid.x >= Parameters.ActiveWidth || tid.y >= Parameters.ActiveHeight)
	{
		return;
	}
	uint value = Parameters.Seed + tid.x + 17 * tid.y;
	if (Parameters.Pass == 0)
	{
		FloatOutput[tid.xy] = float4(value, value + 1, value + 2, 1);
		UintOutput[tid.xy] = value * 3 + 2;
		IntOutput[tid.xy] = -int(value);
	}
	else
	{
		FloatOutput[tid.xy] += float4(2, 4, 8, 0);
		UintOutput[tid.xy] += 5;
		IntOutput[tid.xy] -= 7;
	}
}
