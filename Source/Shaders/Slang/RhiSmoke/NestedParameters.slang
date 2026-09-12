struct ResourcePair
{
	StructuredBuffer<uint> Inputs[2];
	Texture2D<uint> Image;
};

struct ChildParameters
{
	uint Bias;
	ResourcePair Resources;
};

struct Parameters
{
	uint Count;
	uint Scale;
	ResourcePair Resources;
	ParameterBlock<ChildParameters> Child;
};

[[vk::binding(0, 2)]] ParameterBlock<Parameters> Settings;
[[vk::binding(0, 5)]] RWStructuredBuffer<uint> Output;

[shader("compute")]
[numthreads(8, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x < Settings.Count)
	{
		Output[id.x] = Settings.Scale * (Settings.Resources.Inputs[0][id.x] + Settings.Resources.Inputs[1][id.x]) +
			Settings.Resources.Image.Load(int3(0, 0, 0)) + Settings.Child.Bias +
			Settings.Child.Resources.Inputs[0][id.x] + Settings.Child.Resources.Inputs[1][id.x] +
			Settings.Child.Resources.Image.Load(int3(0, 0, 0));
	}
}
