// Split-sum BRDF LUT (item 61): texel (x, y) holds (A, B, 0, 1) for
// N.V = (x + 0.5) / size and roughness = (y + 0.5) / size. Mirrors
// Environment::BuildBrdfLut; bindings follow EnvironmentBrdfLutBindings.
#include "Environment/EnvironmentCommon.slang"

struct BrdfLutConstants
{
	uint Size;
	uint SampleCount;
	uint2 Reserved;
};

[[vk::push_constant]] ConstantBuffer<BrdfLutConstants> Constants;
[[vk::binding(0, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Destination;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Constants.Size || id.y >= Constants.Size)
	{
		return;
	}
	const float nDotV = (float(id.x) + 0.5) / float(Constants.Size);
	const float roughness = (float(id.y) + 0.5) / float(Constants.Size);
	Destination[id.xy] = float4(EnvironmentIntegrateBrdf(nDotV, roughness, Constants.SampleCount), 0.0, 1.0);
}
