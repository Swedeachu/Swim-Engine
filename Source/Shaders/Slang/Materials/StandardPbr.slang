// Standard metallic-roughness shading (critical-path item 60). Mirrors
// Engine/Systems/Renderer/Materials/StandardPbr.cpp line for line: Lambert diffuse,
// GGX distribution, height-correlated Smith visibility, Schlick Fresnel with
// F0 = 0.04 for dielectrics. Colors are linear; sRGB textures are decoded by the
// sampler (RGBA8UnormSrgb views). Texture index 0 is the bindless fallback (white),
// and a zero NormalTexture means "no normal map". Shaders sample the textures from
// their own bindless arrays (see RhiSmoke/StandardMaterialDraw.slang) and pass the
// texels in.
#ifndef SWIM_STANDARD_PBR_SLANG
#define SWIM_STANDARD_PBR_SLANG
#include "Materials/StandardMaterialParameters.slang"

static const float StandardPbrPi = 3.14159265358979323846;
static const float StandardPbrMinPerceptualRoughness = 0.045;
static const float StandardPbrDielectricF0 = 0.04;

float StandardPbrDistributionGgx(float nDotH, float alpha)
{
	const float a2 = alpha * alpha;
	const float f = (nDotH * a2 - nDotH) * nDotH + 1.0;
	return a2 / (StandardPbrPi * f * f);
}

float StandardPbrVisibilitySmithGgxCorrelated(float nDotV, float nDotL, float alpha)
{
	const float a2 = alpha * alpha;
	const float ggxV = nDotL * sqrt(nDotV * nDotV * (1.0 - a2) + a2);
	const float ggxL = nDotV * sqrt(nDotL * nDotL * (1.0 - a2) + a2);
	return 0.5 / (ggxV + ggxL);
}

float3 StandardPbrFresnelSchlick(float3 f0, float vDotH)
{
	const float f = pow(1.0 - vDotH, 5.0);
	return f0 + (1.0 - f0) * f;
}

// BRDF times N.L for unit vectors pointing away from the surface.
float3 StandardPbrEvaluateBrdf(float3 baseColor, float metallicInput, float perceptualRoughness, float3 normal, float3 view, float3 light)
{
	const float nDotL = dot(normal, light);
	if (nDotL <= 0.0)
	{
		return float3(0.0, 0.0, 0.0);
	}
	const float nDotV = max(dot(normal, view), 1.0e-4);
	const float3 halfVector = normalize(view + light);
	const float nDotH = clamp(dot(normal, halfVector), 0.0, 1.0);
	const float vDotH = clamp(dot(view, halfVector), 0.0, 1.0);
	const float roughness = clamp(perceptualRoughness, StandardPbrMinPerceptualRoughness, 1.0);
	const float alpha = roughness * roughness;
	const float metallic = clamp(metallicInput, 0.0, 1.0);
	const float3 f0 = StandardPbrDielectricF0 + (baseColor - StandardPbrDielectricF0) * metallic;
	const float3 diffuseColor = baseColor * (1.0 - metallic);
	const float3 fresnel = StandardPbrFresnelSchlick(f0, vDotH);
	const float dv = StandardPbrDistributionGgx(nDotH, alpha) * StandardPbrVisibilitySmithGgxCorrelated(nDotV, nDotL, alpha);
	const float3 diffuse = (1.0 - fresnel) * diffuseColor / StandardPbrPi;
	return (diffuse + dv * fresnel) * nDotL;
}

struct StandardPbrTexels
{
	float4 BaseColor;
	float3 MetallicRoughness;
	float3 TangentNormal; // In [-1, 1]; ignored unless HasNormal.
	bool HasNormal;
	float Occlusion;
	float3 Emissive;
};

struct StandardPbrLighting
{
	float3 View;
	float3 LightDirection;
	float3 LightRadiance;
	float3 Ambient;
};

// Linear color and alpha; Discard is set when alpha masking rejects the pixel.
struct StandardPbrResult
{
	float4 Color;
	bool Discard;
};

StandardPbrResult StandardPbrShade(StandardMaterialParameters parameters, StandardPbrTexels texels, float3 geometricNormal, float3 tangent,
	float3 bitangent, bool frontFacing, StandardPbrLighting lighting)
{
	StandardPbrResult result;
	const float alpha = parameters.BaseColorFactor.a * texels.BaseColor.a;
	result.Discard = (parameters.Flags & StandardMaterialFlagAlphaMask) != 0 && alpha < parameters.AlphaCutoff;
	const bool flip = !frontFacing && (parameters.Flags & StandardMaterialFlagDoubleSided) != 0;
	float3 normal = flip ? -geometricNormal : geometricNormal;
	if (texels.HasNormal)
	{
		const float3 n = normalize(float3(texels.TangentNormal.xy * parameters.NormalScale, texels.TangentNormal.z));
		normal = normalize(tangent * n.x + bitangent * n.y + normal * n.z);
	}
	const float3 baseColor = parameters.BaseColorFactor.rgb * texels.BaseColor.rgb;
	const float metallic = parameters.MetallicFactor * texels.MetallicRoughness.b;
	const float roughness = parameters.RoughnessFactor * texels.MetallicRoughness.g;
	const float occlusion = 1.0 + parameters.OcclusionStrength * (texels.Occlusion - 1.0);
	const float3 brdf = StandardPbrEvaluateBrdf(baseColor, metallic, roughness, normal, lighting.View, lighting.LightDirection);
	const float3 color = brdf * lighting.LightRadiance + lighting.Ambient * baseColor * occlusion + parameters.EmissiveFactor * texels.Emissive;
	result.Color = float4(color, alpha);
	return result;
}

#endif
