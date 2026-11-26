// Clustered Forward+ shading (critical-path items 66-67). One source, two programs:
//  - SwimForwardOpaque: opaque and alpha-masked materials; writes linear HDR color
//    (alpha 1) and ObjectId + 1, depth-tested and written;
//  - SwimForwardTransparent (FORWARD_TRANSPARENT=1): StandardPbr::FlagAlphaBlend
//    materials drawn back to front; writes premultiplied color for One /
//    OneMinusSourceAlpha blending, without depth writes.
// Draws come from GPU visibility (the instance id is the draw slot); vertices are
// pulled from the slot's GeometryHeap vertex page as StandardVertex (12 floats).
// Each pixel: StandardPbrResolve -> every directional light + its cluster's local
// lights, each times its shadow factor (Shadows/ShadowRecords.slang) + ambient +
// optional IBL + emission. Mirrors ForwardPlus::Shade; bindings and lighting come from
// ForwardFrame.slang (ForwardPlusDrawBindings).
#ifndef FORWARD_TRANSPARENT
#define FORWARD_TRANSPARENT 0
#endif
// Depth prepass (SwimForwardDepth): the opaque vertex stage and a fragment stage that only
// rejects culled faces and alpha-masked texels, writing depth. The opaque colour pass that
// follows it (SwimForwardOpaquePrepassed, FORWARD_PREPASSED=1) tests that depth (reverse-Z
// GreaterOrEqual, no depth writes) with early fragment tests, so each pixel is shaded once
// instead of once per overlapping surface. It still discards culled faces and masked
// texels exactly like the prepass: those left no depth, so they would pass the test over
// the surface behind them.
#ifndef FORWARD_DEPTH_ONLY
#define FORWARD_DEPTH_ONLY 0
#endif
#ifndef FORWARD_PREPASSED
#define FORWARD_PREPASSED 0
#endif
// SwimForwardBackDepth (FORWARD_DEPTH_ONLY=1, FORWARD_BACK_DEPTH=1): keeps only back faces
// (whatever the material's culling), for the nearest back face per pixel: the far side of
// every visible closed surface (ForwardPlusTargets::BackDepth).
#ifndef FORWARD_BACK_DEPTH
#define FORWARD_BACK_DEPTH 0
#endif
// SwimForwardOpaqueDeferred (FORWARD_PREPASSED=1, FORWARD_DEFERRED_LOCAL=1): the prepassed
// opaque pass without the clustered local lights, writing the surface's base colour and
// metalness to an eighth target; ForwardLocalLights.slang adds the local lights after it.
#ifndef FORWARD_DEFERRED_LOCAL
#define FORWARD_DEFERRED_LOCAL 0
#endif
#include "ForwardPlus/ForwardFrame.slang"

struct VertexOutput
{
	float4 Position : SV_Position;
	float3 WorldPosition : WORLD_POSITION;
	float3 Normal : NORMAL;
	float4 Tangent : TANGENT; // w: bitangent sign, already flipped for mirroring transforms.
	float2 Uv : TEXCOORD0;
	nointerpolation uint Material : MATERIAL;
#if !FORWARD_TRANSPARENT
	// Opaque only: an output the transparent fragment stage never reads would draw a
	// validation warning (Shader-OutputNotConsumed).
	nointerpolation uint ObjectId : OBJECT_ID;
	// Unjittered clip positions of this frame and the previous one (motion vectors).
	float4 CurrentClip : CURRENT_CLIP;
	float4 PreviousClip : PREVIOUS_CLIP;
#endif
	nointerpolation uint Mirrored : MIRRORED;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VulkanVertexID, uint drawSlot : SV_VulkanInstanceID)
{
	const GpuDrawRecord record = DrawRecords[drawSlot];
	const GpuInstanceRecord instance = Instances[record.InstanceRow];
	// Loops copy the transform and vertex once each: GPU-assisted validation instruments
	// every storage-buffer load and flags shaders with more than 75 of them, and Slang
	// otherwise re-loads at each use.
	float4 rows[6];
	for (uint row = 0; row < 6; ++row)
	{
		rows[row] = row < 3 ? Transforms[instance.TransformIndex].Current[row] : Transforms[instance.TransformIndex].Previous[row - 3];
	}
	float4 current[3] = { rows[0], rows[1], rows[2] };
	// This frame's and the previous frame's view-projection rows, copied once each.
	float4 projections[8];
	for (uint row = 0; row < 8; ++row)
	{
		projections[row] = row < 4 ? Views[0].ViewProjection[row] : Views[0].PreviousViewProjection[row - 4];
	}
	float4 viewProjection[4] = { projections[0], projections[1], projections[2], projections[3] };
	const uint base = vertexId * ForwardVertexFloats;
#if FORWARD_TRANSPARENT
	float v[ForwardVertexFloats];
	for (uint i = 0; i < ForwardVertexFloats; ++i)
	{
		v[i] = Vertices[base + i];
	}
#else
	// Three more floats: the previous-frame local position, PreviousVertexOffset
	// vertices further in the same page for GPU-skinned meshes (item 78), or this
	// vertex's own position again (offset 0). Still one load site.
	const uint previousBase = (vertexId + instance.PreviousVertexOffset) * ForwardVertexFloats;
	float v[ForwardVertexFloats + 3];
	for (uint i = 0; i < ForwardVertexFloats + 3; ++i)
	{
		v[i] = Vertices[i < ForwardVertexFloats ? base + i : previousBase + (i - ForwardVertexFloats)];
	}
#endif
	const float3 position = float3(v[0], v[1], v[2]);
	const float3 normal = float3(v[3], v[4], v[5]);
	const float4 tangent = float4(v[6], v[7], v[8], v[9]);
	const float2 uv = float2(v[10], v[11]);

	// precise: the depth prepass (FORWARD_DEPTH_ONLY) and the prepassed shading pass must
	// rasterize bit-identical depths, so no FMA contraction may differ between variants.
	precise const float3 world = GpuSceneTransformPoint(current, position);
	const bool mirrored = ForwardDeterminant(current) < 0.0;
	const float4 p = float4(world, 1.0);
	VertexOutput output;
	precise float4 clip = ForwardProject(viewProjection, p);
	const float4 jitterOverlay = Views[0].JitterOverlay;
	const bool overlay = (instance.Flags & GpuSceneFlagOverlay) != 0u;
	if (overlay)
	{
		clip = ForwardOverlayClip(jitterOverlay.zw, clip);
	}
	// TAA jitter (ForwardPlus::BuildForwardViewRecord): an NDC offset, so clip.xy moves by jitter * w.
	precise const float4 jittered = float4(clip.xy + jitterOverlay.xy * clip.w, clip.zw);
	output.Position = jittered;
	output.WorldPosition = world;
	output.Normal = ForwardTransformNormal(current, normal);
	output.Tangent = float4(ForwardTransformDirection(current, tangent.xyz), (tangent.w < 0.0) != mirrored ? -1.0 : 1.0);
	output.Uv = uv;
	// The table's rows (MaterialCount is its capacity): a length query, not another load.
	uint materialRows;
	uint materialStride;
	Materials.GetDimensions(materialRows, materialStride);
	output.Material = instance.MaterialSet < materialRows ? instance.MaterialSet : 0u;
#if !FORWARD_TRANSPARENT
	output.ObjectId = instance.ObjectId;
	float4 previous[3] = { rows[3], rows[4], rows[5] };
	output.CurrentClip = clip;
	float4 previousViewProjection[4] = { projections[4], projections[5], projections[6], projections[7] };
	const float3 previousPosition = float3(v[ForwardVertexFloats], v[ForwardVertexFloats + 1], v[ForwardVertexFloats + 2]);
	const float4 previousClip = ForwardProject(previousViewProjection, float4(GpuSceneTransformPoint(previous, previousPosition), 1.0));
	output.PreviousClip = overlay ? ForwardOverlayClip(jitterOverlay.zw, previousClip) : previousClip;
#endif
	output.Mirrored = mirrored ? 1u : 0u;
	return output;
}

struct ShadedPixel
{
	float4 Color;	 // Linear radiance, straight alpha.
	float3 Indirect; // Ambient + IBL (ForwardPlus::IndirectRadiance), already in Color.
	float3 Reflectance; // Split-sum specular reflectance (ForwardPlus::SpecularReflectance).
	float3 Specular;	// Specular IBL (ForwardPlus::SpecularRadiance), part of Indirect.
	float3 Normal;	 // World shading normal.
	float Roughness; // Perceptual.
	float3 BaseColor;
	float Metallic;
	float4 Debug;
	bool UseDebug;
};

ShadedPixel ShadePixel(VertexOutput input, bool rasterFrontFacing)
{
	const ForwardView view = Views[0];
	const ClusterGrid grid = Grid[0];
	const StandardMaterialParameters material = Materials[input.Material];
	// Derivatives before any discard (uniform control flow): the glint's texture layers.
	const float2 uvDx = ddx(input.Uv);
	const float2 uvDy = ddy(input.Uv);
	// ForwardPlus::BuildFrame: the rasterizer's facing, corrected for mirroring transforms.
	const bool frontFacing = rasterFrontFacing != (input.Mirrored != 0u);
	// Also after the depth prepass: the prepass wrote no depth where it rejected a culled
	// face or a masked texel, so the surface behind is there and this fragment (nearer)
	// passes the GreaterOrEqual test. Without the discard it would be shaded over that
	// surface, and which one wins depends on the (unordered, GPU-compacted) draw order:
	// alpha-tested foliage flickered between its leaves and its whole card.
	if (ForwardCullsFace(material, frontFacing))
	{
		discard;
	}
	const SamplerState materialSampler = Samplers[NonUniformResourceIndex(material.MaterialSampler)];
	StandardPbrTexels texels;
	texels.BaseColor = Textures[NonUniformResourceIndex(material.BaseColorTexture)].Sample(materialSampler, input.Uv);
	texels.MetallicRoughness = Textures[NonUniformResourceIndex(material.MetallicRoughnessTexture)].Sample(materialSampler, input.Uv).rgb;
	texels.HasNormal = material.NormalTexture != 0;
	texels.TangentNormal = Textures[NonUniformResourceIndex(material.NormalTexture)].Sample(materialSampler, input.Uv).rgb * 2.0 - 1.0;
	texels.Occlusion = Textures[NonUniformResourceIndex(material.OcclusionTexture)].Sample(materialSampler, input.Uv).r;
	texels.Emissive = Textures[NonUniformResourceIndex(material.EmissiveTexture)].Sample(materialSampler, input.Uv).rgb;

	const float3 normal = normalize(input.Normal);
	const float3 tangent = normalize(input.Tangent.xyz - normal * dot(normal, input.Tangent.xyz) + float3(1.0e-20, 0.0, 0.0));
	const float3 bitangent = cross(normal, tangent) * (input.Tangent.w < 0.0 ? -1.0 : 1.0);
	StandardPbrSurface surface = StandardPbrResolve(material, texels, normal, tangent, bitangent, frontFacing);
	if (surface.Discard)
	{
		discard;
	}
	if (material.GlintIntensity > 0.0 && view.Glint.Source.x != GlintModeOff)
	{
		const float3 toCamera = normalize(view.CameraPosition - input.WorldPosition);
		const float3 glint = GlintEvaluate(view.Glint, Textures[NonUniformResourceIndex(view.Glint.Source.y)],
			Samplers[NonUniformResourceIndex(view.Glint.Source.z)], input.Uv, uvDx, uvDy, surface.Normal, toCamera);
		surface.Emissive += glint * material.GlintIntensity;
	}

	const ForwardLighting lit = ForwardLightSurface(view, grid, surface, input.WorldPosition, input.Position.xy);
	ShadedPixel pixel;
	pixel.Specular = lit.Specular;
	pixel.Reflectance = lit.Reflectance;
	pixel.Indirect = lit.Indirect;
	pixel.Color = float4(lit.Radiance, surface.Alpha);
	pixel.Normal = surface.Normal;
	pixel.Roughness = surface.PerceptualRoughness;
	pixel.BaseColor = surface.BaseColor;
	pixel.Metallic = surface.Metallic;
	pixel.UseDebug = view.DebugMode == ForwardDebugClusterHeatmap;
	pixel.Debug = float4(0.0, 0.0, 0.0, 1.0);
	if (pixel.UseDebug)
	{
		const float viewDepth = -dot(grid.ViewRow2, float4(input.WorldPosition, 1.0));
		const ClusterRecord record = ClusterRecords[ClusterIndexFor(grid, input.Position.x, input.Position.y, viewDepth)];
		const float4 heat = ClusterHeatmapColor(record.Count, record.RawCount, grid.Limits.z);
		pixel.Debug = heat.a > 0.0 ? heat : float4(0.0, 0.0, 0.0, 1.0);
	}
	return pixel;
}

#if FORWARD_TRANSPARENT
struct TransparentOutput
{
	float4 Color : SV_Target0;	  // Premultiplied.
	float4 Indirect : SV_Target1; // (0, 0, 0, alpha): the blend scales the opaque indirect radiance by the transmittance.
	float4 Reflectance : SV_Target2; // The same for the specular reflectance and radiance (item 76, SSR).
	float4 Specular : SV_Target3;
};

[shader("fragment")]
TransparentOutput fragmentMain(VertexOutput input, bool frontFacing : SV_IsFrontFace)
{
	const ShadedPixel pixel = ShadePixel(input, frontFacing);
	TransparentOutput output;
	output.Color = float4(pixel.Color.rgb * pixel.Color.a, pixel.Color.a);
	output.Indirect = float4(0.0, 0.0, 0.0, pixel.Color.a);
	output.Reflectance = output.Indirect;
	output.Specular = output.Indirect;
	return output;
}
#else
struct OpaqueOutput
{
	float4 Color : SV_Target0;
	float ObjectId : SV_Target1;
	float2 Velocity : SV_Target2; // ForwardPlus::MotionVector: UV of this frame minus UV of the previous.
	float4 Normal : SV_Target3;	  // World shading normal, perceptual roughness (item 76).
	float4 Indirect : SV_Target4; // Ambient + IBL radiance, 0 in debug views (item 76).
	float4 Reflectance : SV_Target5; // Split-sum specular reflectance, 0 in debug views (item 76, SSR).
	float4 Specular : SV_Target6;	 // Specular IBL radiance (part of Indirect), 0 in debug views.
#if FORWARD_DEFERRED_LOCAL
	float4 Material : SV_Target7; // Base colour, metalness; alpha -1 in debug views (no local lights added).
#endif
};

#if FORWARD_DEPTH_ONLY
// Depth prepass: coverage only.
[shader("fragment")]
void fragmentMain(VertexOutput input, bool rasterFrontFacing : SV_IsFrontFace)
{
	const StandardMaterialParameters material = Materials[input.Material];
	const bool frontFacing = rasterFrontFacing != (input.Mirrored != 0u);
#if FORWARD_BACK_DEPTH
	if (frontFacing)
	{
		discard;
	}
#else
	if (ForwardCullsFace(material, frontFacing))
	{
		discard;
	}
#endif
	if ((material.Flags & StandardMaterialFlagAlphaMask) != 0)
	{
		const SamplerState materialSampler = Samplers[NonUniformResourceIndex(material.MaterialSampler)];
		const float alpha =
			material.BaseColorFactor.a * Textures[NonUniformResourceIndex(material.BaseColorTexture)].Sample(materialSampler, input.Uv).a;
		if (alpha < material.AlphaCutoff)
		{
			discard;
		}
	}
}
#else
#if FORWARD_PREPASSED
[earlydepthstencil]
#endif
[shader("fragment")]
OpaqueOutput fragmentMain(VertexOutput input, bool frontFacing : SV_IsFrontFace)
{
	const ShadedPixel pixel = ShadePixel(input, frontFacing);
	OpaqueOutput output;
	output.Color = pixel.UseDebug ? pixel.Debug : float4(pixel.Color.rgb, 1.0);
	output.ObjectId = float(input.ObjectId + 1u);
	const float2 now = input.CurrentClip.xy / input.CurrentClip.w;
	const float2 before = input.PreviousClip.xy / input.PreviousClip.w;
	output.Velocity = (now - before) * float2(0.5, -0.5);
	output.Normal = float4(pixel.Normal, pixel.Roughness);
	output.Indirect = float4(pixel.UseDebug ? float3(0.0, 0.0, 0.0) : pixel.Indirect, 1.0);
	output.Reflectance = float4(pixel.UseDebug ? float3(0.0, 0.0, 0.0) : pixel.Reflectance, 1.0);
	output.Specular = float4(pixel.UseDebug ? float3(0.0, 0.0, 0.0) : pixel.Specular, 1.0);
#if FORWARD_DEFERRED_LOCAL
	output.Material = float4(pixel.BaseColor, pixel.UseDebug ? -1.0 : pixel.Metallic);
#endif
	return output;
}
#endif // FORWARD_DEPTH_ONLY
#endif
