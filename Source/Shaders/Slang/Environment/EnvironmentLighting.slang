// Shader-side environment lookup (item 61): SH irradiance at the normal, the
// prefiltered cube at the reflection vector with lod = roughness * (mips - 1), and
// the split-sum LUT at (N.V, roughness); radiance terms scale by the intensity and
// lookups rotate around +Y. Mirrors Environment::EnvironmentProbe::Lookup and feeds
// StandardPbrShadeResolved.
#ifndef SWIM_ENVIRONMENT_LIGHTING_SLANG
#define SWIM_ENVIRONMENT_LIGHTING_SLANG
#include "Environment/EnvironmentCommon.slang"

float3 EnvironmentEvaluateIrradiance(StructuredBuffer<float4> coefficients, float3 normal)
{
	float basis[9];
	EnvironmentShBasis(normal, basis);
	float3 result = float3(0.0, 0.0, 0.0);
	for (uint i = 0; i < 9; ++i)
	{
		result += coefficients[i].rgb * basis[i];
	}
	return max(result, float3(0.0, 0.0, 0.0));
}

StandardPbrEnvironment EnvironmentLookup(StructuredBuffer<float4> irradiance, TextureCube<float4> prefiltered, uint prefilteredMipCount,
	Texture2D<float4> brdfLut, SamplerState linearClamp, float intensity, float rotation, StandardPbrSurface surface, float3 view)
{
	const float3 reflected = 2.0 * dot(surface.Normal, view) * surface.Normal - view;
	const float roughness = clamp(surface.PerceptualRoughness, StandardPbrMinPerceptualRoughness, 1.0);
	const float nDotV = clamp(dot(surface.Normal, view), 1.0e-4, 1.0);
	StandardPbrEnvironment environment;
	environment.Irradiance = EnvironmentEvaluateIrradiance(irradiance, EnvironmentRotateLookup(surface.Normal, rotation)) * intensity;
	environment.Prefiltered = prefiltered.SampleLevel(linearClamp, EnvironmentRotateLookup(reflected, rotation),
		EnvironmentPrefilterLodForRoughness(roughness, prefilteredMipCount)).rgb * intensity;
	const float2 ab = brdfLut.SampleLevel(linearClamp, float2(nDotV, roughness), 0.0).rg;
	environment.BrdfScale = ab.x;
	environment.BrdfBias = ab.y;
	return environment;
}

#endif
