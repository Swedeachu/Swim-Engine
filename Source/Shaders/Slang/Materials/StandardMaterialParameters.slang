// Parameter record of the standard metallic-roughness material (critical-path item
// 58). The C++ MaterialTemplate layout is derived from this struct's reflection
// (Tools/ShaderCompiler/ShaderMaterialLayout.h); uint fields ending in "Texture"
// or "Sampler" are bindless indices (BindlessResourceTable, 0 = fallback).
#ifndef SWIM_STANDARD_MATERIAL_PARAMETERS_SLANG
#define SWIM_STANDARD_MATERIAL_PARAMETERS_SLANG

static const uint StandardMaterialFlagAlphaMask = 1u << 0;
static const uint StandardMaterialFlagDoubleSided = 1u << 1;

struct StandardMaterialParameters
{
	float4 BaseColorFactor;
	float3 EmissiveFactor;
	float MetallicFactor;
	float RoughnessFactor;
	float NormalScale;
	float OcclusionStrength;
	float AlphaCutoff;
	uint BaseColorTexture;
	uint MetallicRoughnessTexture;
	uint NormalTexture;
	uint OcclusionTexture;
	uint EmissiveTexture;
	uint MaterialSampler;
	uint Flags;
	uint Reserved;
};

#endif
