// Order-2 SH irradiance (item 61): one 64-thread group projects the six faces of
// one source mip, every texel weighted by its exact solid angle, then scales band l
// by A_l / pi so the output is irradiance / pi. Mirrors
// Environment::ProjectIrradianceSh; bindings follow EnvironmentIrradianceBindings.
#include "Environment/EnvironmentCommon.slang"

struct IrradianceConstants
{
	uint FaceSize;
	uint Reserved0;
	uint Reserved1;
	uint Reserved2;
};

[[vk::push_constant]] ConstantBuffer<IrradianceConstants> Constants;
[[vk::binding(0, 0)]] Texture2DArray<float4> Source;
[[vk::binding(1, 0)]] RWStructuredBuffer<float4> Output;

static const uint ThreadCount = 64;
groupshared float3 Partial[9][ThreadCount];

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 threadId : SV_GroupThreadID)
{
	const uint thread = threadId.x;
	const uint size = Constants.FaceSize;
	const uint texelCount = 6 * size * size;
	float3 sums[9];
	for (uint i = 0; i < 9; ++i)
	{
		sums[i] = float3(0.0, 0.0, 0.0);
	}
	for (uint texel = thread; texel < texelCount; texel += ThreadCount)
	{
		const uint face = texel / (size * size);
		const uint rest = texel - face * size * size;
		const uint y = rest / size;
		const uint x = rest - y * size;
		float basis[9];
		EnvironmentShBasis(EnvironmentCubeTexelDirection(face, x, y, size), basis);
		const float3 radiance = Source.Load(int4(int(x), int(y), int(face), 0)).rgb * EnvironmentCubeTexelSolidAngle(x, y, size);
		for (uint i = 0; i < 9; ++i)
		{
			sums[i] += radiance * basis[i];
		}
	}
	for (uint i = 0; i < 9; ++i)
	{
		Partial[i][thread] = sums[i];
	}
	GroupMemoryBarrierWithGroupSync();
	for (uint stride = ThreadCount / 2; stride > 0; stride /= 2)
	{
		if (thread < stride)
		{
			for (uint i = 0; i < 9; ++i)
			{
				Partial[i][thread] += Partial[i][thread + stride];
			}
		}
		GroupMemoryBarrierWithGroupSync();
	}
	if (thread < 9)
	{
		Output[thread] = float4(Partial[thread][0] * EnvironmentShIrradianceScale(thread), 0.0);
	}
}
