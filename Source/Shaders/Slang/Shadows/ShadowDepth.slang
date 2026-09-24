// Shadow depth rendering (Phase 16, items 70-72). One source, two programs:
//  - SwimShadowDepth: depth only (the fragment stage writes nothing);
//  - SwimShadowMasked (SHADOW_ALPHA_TEST=1): alpha-masked casters sample base-color
//    alpha through the bindless table and discard below the cutoff.
// Draws come from a shadow view's GPU caster cull (the instance id is the draw slot);
// vertices are pulled as StandardVertex. The push constant names the GpuShadowView.
// Bindings follow ShadowDepthBindings.
#include "GpuScene/GpuSceneRecords.slang"
#include "GpuScene/VisibilityRecords.slang"
#include "Materials/StandardMaterialParameters.slang"
#include "Shadows/ShadowRecords.slang"

#ifndef SHADOW_ALPHA_TEST
#define SHADOW_ALPHA_TEST 0
#endif

struct ShadowConstants
{
	uint ViewIndex;
	uint MaterialCount;
	uint Reserved0;
	uint Reserved1;
};

[[vk::binding(0, 0)]] StructuredBuffer<GpuInstanceRecord> Instances;
[[vk::binding(1, 0)]] StructuredBuffer<GpuTransformRecord> Transforms;
[[vk::binding(2, 0)]] StructuredBuffer<GpuDrawRecord> DrawRecords;
[[vk::binding(3, 0)]] StructuredBuffer<float> Vertices;
[[vk::binding(4, 0)]] StructuredBuffer<GpuShadowView> Views;
[[vk::binding(5, 0)]] StructuredBuffer<StandardMaterialParameters> Materials;
[[vk::push_constant]] ConstantBuffer<ShadowConstants> Constants;
#if SHADOW_ALPHA_TEST
[[vk::binding(0, 1)]] SamplerState Samplers[];
[[vk::binding(1, 1)]] Texture2D<float4> Textures[];
#endif

static const uint ShadowVertexFloats = 12;

struct VertexOutput
{
	float4 Position : SV_Position;
#if SHADOW_ALPHA_TEST
	// Only the masked variant reads these (an unread varying is a validation warning).
	float2 Uv : TEXCOORD0;
	nointerpolation uint Material : MATERIAL;
#endif
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VulkanVertexID, uint drawSlot : SV_VulkanInstanceID)
{
	const GpuDrawRecord record = DrawRecords[drawSlot];
	const GpuInstanceRecord instance = Instances[record.InstanceRow];
	const uint base = vertexId * ShadowVertexFloats;
	const float3 local = float3(Vertices[base], Vertices[base + 1], Vertices[base + 2]);
	const float4 p = float4(GpuSceneTransformPoint(Transforms[instance.TransformIndex].Current, local), 1.0);
	const GpuShadowView view = Views[Constants.ViewIndex];
	VertexOutput output;
	output.Position = float4(dot(view.ViewProjection[0], p), dot(view.ViewProjection[1], p), dot(view.ViewProjection[2], p),
		dot(view.ViewProjection[3], p));
#if SHADOW_ALPHA_TEST
	output.Uv = float2(Vertices[base + 10], Vertices[base + 11]);
	output.Material = instance.MaterialSet < Constants.MaterialCount ? instance.MaterialSet : 0u;
#endif
	return output;
}

#if SHADOW_ALPHA_TEST
[shader("fragment")]
void fragmentMain(VertexOutput input)
{
	const StandardMaterialParameters material = Materials[input.Material];
	const SamplerState materialSampler = Samplers[NonUniformResourceIndex(material.MaterialSampler)];
	const float alpha =
		material.BaseColorFactor.a * Textures[NonUniformResourceIndex(material.BaseColorTexture)].Sample(materialSampler, input.Uv).a;
	if ((material.Flags & StandardMaterialFlagAlphaMask) != 0 && alpha < material.AlphaCutoff)
	{
		discard;
	}
}
#else
// Depth only. No parameters: an input struct holding only SV_Position reflects
// without a varying binding, which the RHI interface conversion rejects.
[shader("fragment")]
void fragmentMain()
{
}
#endif
