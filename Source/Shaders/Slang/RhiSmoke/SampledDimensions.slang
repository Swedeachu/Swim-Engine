#ifndef SWIM_TEST_CUBE_ARRAY
#define SWIM_TEST_CUBE_ARRAY 0
#endif

[[vk::binding(0, 0)]] Texture1D<uint> Line;
[[vk::binding(1, 0)]] Texture1DArray<uint> Lines;
[[vk::binding(3, 0)]] Texture3D<uint> Volume;
[[vk::binding(4, 0)]] TextureCube<float> Cube;
[[vk::binding(5, 0)]] Texture2D<uint> Plane;
#if SWIM_TEST_CUBE_ARRAY
[[vk::binding(6, 0)]] TextureCubeArray<float> Cubes;
#endif
[[vk::binding(7, 0)]] SamplerState PointSampler;
[[vk::binding(8, 0)]] RWStructuredBuffer<uint> Output;

[shader("compute")]
[numthreads(4, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID,
	[[vk::binding(2, 0)]] uniform Texture2DArray<uint> Planes)
{
#if SWIM_TEST_CUBE_ARRAY
	// Slang 2026.16.1 omits this capability for TextureCubeArray. Declare it
	// explicitly; the smoke selects this variant only with imageCubeArray enabled.
	spirv_asm
	{
		OpCapability SampledCubeArray;
	};
#endif
	if (id.x >= 12)
	{
		return;
	}
	uint i = id.x;
	int x = int(i % 4);
	int y = int((i / 4) % 4);
	int z = int(i % 3);
	int layer = int(i % 2);
	float3 directions[6] = { float3(1, 0, 0), float3(-1, 0, 0), float3(0, 1, 0),
		float3(0, -1, 0), float3(0, 0, 1), float3(0, 0, -1) };
	float3 direction = directions[i % 6];
	// Mip zero is relative to each view's nonzero base mip.
	Output[i * 8] = Line.Load(int2(x, 0));
	Output[i * 8 + 1] = Lines.Load(int3(x, layer, 0));
	Output[i * 8 + 2] = Planes.Load(int4(x, y, layer, 0));
	Output[i * 8 + 3] = Volume.Load(int4(x, y, z, 0));
	Output[i * 8 + 4] = uint(Cube.SampleLevel(PointSampler, direction, 0));
	Output[i * 8 + 5] = Plane.Load(int3(x, y, 0));
#if SWIM_TEST_CUBE_ARRAY
	Output[i * 8 + 6] = uint(Cubes.SampleLevel(PointSampler, float4(direction, i / 6), 0));
#endif
	// Column seven and threads 12..15 remain untouched guards.
}
