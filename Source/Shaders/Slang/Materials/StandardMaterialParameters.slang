// Parameter record of the standard metallic-roughness material (critical-path item
// 58). The C++ MaterialTemplate layout is derived from this struct's reflection
// (Tools/ShaderCompiler/ShaderMaterialLayout.h); uint fields ending in "Texture"
// or "Sampler" are bindless indices (BindlessResourceTable, 0 = fallback).
#ifndef SWIM_STANDARD_MATERIAL_PARAMETERS_SLANG
#define SWIM_STANDARD_MATERIAL_PARAMETERS_SLANG

static const uint StandardMaterialFlagAlphaMask = 1u << 0;
static const uint StandardMaterialFlagDoubleSided = 1u << 1;
static const uint StandardMaterialFlagAlphaBlend = 1u << 2; // Drawn by the sorted transparent Forward+ pass.

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
	float GlintIntensity; // > 0: the view's glint (ForwardView::Glint) is added to the emission, scaled by it.
};

// The same 80-byte record as five rows. Programs near the GPU-assisted validation budget
// bind StructuredBuffer<StandardMaterialRows> and copy a record in one loop: one
// instrumented load instead of one per field.
struct StandardMaterialRows
{
	float4 Rows[5];
};

StandardMaterialParameters StandardMaterialFromRows(float4 rows[5])
{
	StandardMaterialParameters material;
	material.BaseColorFactor = rows[0];
	material.EmissiveFactor = rows[1].xyz;
	material.MetallicFactor = rows[1].w;
	material.RoughnessFactor = rows[2].x;
	material.NormalScale = rows[2].y;
	material.OcclusionStrength = rows[2].z;
	material.AlphaCutoff = rows[2].w;
	material.BaseColorTexture = asuint(rows[3].x);
	material.MetallicRoughnessTexture = asuint(rows[3].y);
	material.NormalTexture = asuint(rows[3].z);
	material.OcclusionTexture = asuint(rows[3].w);
	material.EmissiveTexture = asuint(rows[4].x);
	material.MaterialSampler = asuint(rows[4].y);
	material.Flags = asuint(rows[4].z);
	material.GlintIntensity = rows[4].w;
	return material;
}

#endif
