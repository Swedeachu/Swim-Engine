// GGX-prefiltered specular environment (item 61): one dispatch per face of one
// destination mip. Mip m holds roughness m / (mips - 1); roughness 0 copies the
// source at lod 0, the rest importance-sample the GGX lobe with N = V = R and read
// the source mip chosen by filtered importance sampling. Mirrors
// Environment::PrefilterDirection; bindings follow EnvironmentPrefilterBindings.
#include "Environment/EnvironmentCommon.slang"

struct PrefilterConstants
{
	uint Face;
	uint Size;
	float Roughness;
	uint SampleCount;
	uint SourceSize;
	uint SourceMipCount;
	uint2 Reserved;
};

[[vk::push_constant]] ConstantBuffer<PrefilterConstants> Constants;
[[vk::binding(0, 0)]] TextureCube<float4> Source;
[[vk::binding(1, 0)]] SamplerState SourceSampler;
[[vk::binding(2, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Destination;

float3 Prefilter(float3 normal)
{
	if (Constants.Roughness <= 0.0 || Constants.SampleCount == 0)
	{
		return Source.SampleLevel(SourceSampler, normal, 0.0).rgb;
	}
	const float roughness = clamp(Constants.Roughness, StandardPbrMinPerceptualRoughness, 1.0);
	const float alpha = roughness * roughness;
	float3 sum = float3(0.0, 0.0, 0.0);
	float weight = 0.0;
	for (uint i = 0; i < Constants.SampleCount; ++i)
	{
		const float3 local = EnvironmentImportanceSampleGgx(EnvironmentHammersley(i, Constants.SampleCount), alpha);
		const float3 halfVector = EnvironmentTangentToWorld(local, normal);
		const float nDotH = local.z;
		const float3 light = 2.0 * nDotH * halfVector - normal;
		const float nDotL = dot(normal, light);
		if (nDotL > 0.0)
		{
			const float lod = EnvironmentPrefilterSourceLod(nDotH, alpha, Constants.SampleCount, Constants.SourceSize, Constants.SourceMipCount);
			sum += Source.SampleLevel(SourceSampler, light, lod).rgb * nDotL;
			weight += nDotL;
		}
	}
	return weight > 0.0 ? sum / weight : Source.SampleLevel(SourceSampler, normal, 0.0).rgb;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Constants.Size || id.y >= Constants.Size)
	{
		return;
	}
	const float3 normal = EnvironmentCubeTexelDirection(Constants.Face, id.x, id.y, Constants.Size);
	Destination[id.xy] = float4(Prefilter(normal), 1.0);
}
