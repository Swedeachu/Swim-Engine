// Hierarchical depth reduction (item 50): one dispatch per pair of HZB mips. Each texel keeps
// the farthest depth of the 2x2 source footprint below it (clipped at odd edges), so any HZB
// texel is a conservative "nothing is farther than this" bound for the depth texels it
// covers. A group reduces its 8x8 texels of the first mip, then (NextWidth > 0) their 4x4 of
// the next mip from groupshared memory: each next texel's footprint lies inside the group's
// tile. Mirrors HzbReference::Reduce (Visibility/HzbPyramid.cpp); bindings follow HzbBindings.
struct HzbConstants
{
	uint SourceWidth;
	uint SourceHeight;
	uint DestinationWidth;
	uint DestinationHeight;
	uint FarthestIsMax; // 1 for forward depth; reverse-Z keeps the minimum.
	uint NextWidth;		// 0: only Destination is written.
	uint NextHeight;
};

[[vk::push_constant]] ConstantBuffer<HzbConstants> Constants;
[[vk::binding(0, 0)]] Texture2D<float> Source;
[[vk::binding(1, 0)]] [[vk::image_format("r32f")]] RWTexture2D<float> Destination;
[[vk::binding(2, 0)]] [[vk::image_format("r32f")]] RWTexture2D<float> Next;

groupshared float Tile[8][8];

float Farther(float a, float b)
{
	return Constants.FarthestIsMax != 0 ? max(a, b) : min(a, b);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID, uint3 local : SV_GroupThreadID, uint3 group : SV_GroupID)
{
	float farthest = 0.0;

	if (id.x < Constants.DestinationWidth && id.y < Constants.DestinationHeight)
	{
		const uint sx = id.x * 2;
		const uint sy = id.y * 2;
		farthest = Source.Load(int3(int(sx), int(sy), 0));

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

	if (Constants.NextWidth == 0)
	{
		return;
	}

	Tile[local.y][local.x] = farthest;
	GroupMemoryBarrierWithGroupSync();

	const uint2 next = group.xy * 4 + local.xy;

	if (local.x < 4 && local.y < 4 && next.x < Constants.NextWidth && next.y < Constants.NextHeight)
	{
		// The footprint's first texel is always inside the first mip (it rounds up).
		float reduced = Tile[local.y * 2][local.x * 2];

		for (uint dy = 0; dy < 2; ++dy)
		{
			for (uint dx = 0; dx < 2; ++dx)
			{
				if (next.x * 2 + dx < Constants.DestinationWidth && next.y * 2 + dy < Constants.DestinationHeight)
				{
					reduced = Farther(reduced, Tile[local.y * 2 + dy][local.x * 2 + dx]);
				}
			}
		}

		Next[next] = reduced;
	}
}
