// Hierarchical depth reduction (item 50): one dispatch per HZB mip. Each texel keeps
// the farthest depth of the 2x2 source footprint below it (clipped at odd edges),
// so any HZB texel is a conservative "nothing is farther than this" bound for the
// depth texels it covers. Mirrors HzbReference::Reduce (Visibility/HzbPyramid.cpp);
// bindings follow HzbBindings.
struct HzbConstants
{
	uint SourceWidth;
	uint SourceHeight;
	uint DestinationWidth;
	uint DestinationHeight;
	uint FarthestIsMax; // 1 for forward depth; reverse-Z keeps the minimum.
};

[[vk::push_constant]] ConstantBuffer<HzbConstants> Constants;
[[vk::binding(0, 0)]] Texture2D<float> Source;
[[vk::binding(1, 0)]] [[vk::image_format("r32f")]] RWTexture2D<float> Destination;

float Farther(float a, float b)
{
	return Constants.FarthestIsMax != 0 ? max(a, b) : min(a, b);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Constants.DestinationWidth || id.y >= Constants.DestinationHeight)
	{
		return;
	}
	const uint sx = id.x * 2;
	const uint sy = id.y * 2;
	float farthest = Source.Load(int3(int(sx), int(sy), 0));
	for (uint dy = 0; dy < 2; ++dy)
	{
		for (uint dx = 0; dx < 2; ++dx)
		{
			if (sx + dx < Constants.SourceWidth && sy + dy < Constants.SourceHeight)
			{
				farthest = Farther(farthest, Source.Load(int3(int(sx + dx), int(sy + dy), 0)));
			}
		}
	}
	Destination[id.xy] = farthest;
}
