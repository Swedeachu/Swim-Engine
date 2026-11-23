// The shadow depth pass as a draw sees it (ShadowDepthBindings): render features that
// cast shadows (RenderFeature::DrawGeometry in the Shadow pass) include this file in the
// program they draw shadow tiles with, declare their own resources in space 2 and project
// with ShadowProjectWorld, passing the view row the draw context names
// (RenderFeatureDraw::ShadowView) in their own push constants. The names declared here are
// reserved in those programs. Depth only: the fragment stage writes nothing (or discards).
#ifndef SWIM_SHADOW_FRAME_SLANG
#define SWIM_SHADOW_FRAME_SLANG
#include "GpuScene/GpuSceneRecords.slang"
#include "GpuScene/VisibilityRecords.slang"
#include "Materials/StandardMaterialParameters.slang"
#include "Shadows/ShadowRecords.slang"

[[vk::binding(0, 0)]] StructuredBuffer<GpuInstanceRecord> Instances;
[[vk::binding(1, 0)]] StructuredBuffer<GpuTransformRecord> Transforms;
[[vk::binding(2, 0)]] StructuredBuffer<GpuDrawRecord> DrawRecords;
[[vk::binding(3, 0)]] StructuredBuffer<float> Vertices;
[[vk::binding(4, 0)]] StructuredBuffer<GpuShadowView> Views;
[[vk::binding(5, 0)]] StructuredBuffer<StandardMaterialParameters> Materials;
[[vk::binding(0, 1)]] SamplerState Samplers[];
[[vk::binding(1, 1)]] Texture2D<float4> Textures[];

float4 ShadowProjectWorld(uint viewIndex, float3 world)
{
	const GpuShadowView view = Views[viewIndex];
	const float4 p = float4(world, 1.0);
	return float4(dot(view.ViewProjection[0], p), dot(view.ViewProjection[1], p), dot(view.ViewProjection[2], p),
		dot(view.ViewProjection[3], p));
}

#endif
