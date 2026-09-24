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
// optional IBL + emission. Mirrors ForwardPlus::Shade; bindings follow
// ForwardPlusDrawBindings.
#include "ClusteredLighting/ClusteredLighting.slang"
#include "Environment/EnvironmentLighting.slang"
#include "ForwardPlus/ForwardPlusRecords.slang"
#include "GpuScene/GpuSceneRecords.slang"
#include "GpuScene/VisibilityRecords.slang"
#include "Shadows/ShadowRecords.slang"

#ifndef FORWARD_TRANSPARENT
#define FORWARD_TRANSPARENT 0
#endif

[[vk::binding(0, 0)]] StructuredBuffer<GpuInstanceRecord> Instances;
[[vk::binding(1, 0)]] StructuredBuffer<GpuTransformRecord> Transforms;
[[vk::binding(2, 0)]] StructuredBuffer<GpuDrawRecord> DrawRecords;
[[vk::binding(3, 0)]] StructuredBuffer<float> Vertices;
[[vk::binding(4, 0)]] StructuredBuffer<ForwardView> Views;
[[vk::binding(5, 0)]] StructuredBuffer<StandardMaterialParameters> Materials;
[[vk::binding(6, 0)]] StructuredBuffer<GpuLightRecord> Lights;
[[vk::binding(7, 0)]] StructuredBuffer<GpuLightHeader> LightHeader;
[[vk::binding(8, 0)]] StructuredBuffer<ClusterGrid> Grid;
[[vk::binding(9, 0)]] StructuredBuffer<ClusterRecord> ClusterRecords;
[[vk::binding(10, 0)]] StructuredBuffer<uint> ClusterIndices;
[[vk::binding(11, 0)]] StructuredBuffer<float4> EnvironmentIrradiance;
[[vk::binding(12, 0)]] TextureCube<float4> EnvironmentPrefiltered;
[[vk::binding(13, 0)]] Texture2D<float4> EnvironmentBrdfLut;
[[vk::binding(14, 0)]] SamplerState EnvironmentSampler;
[[vk::binding(15, 0)]] Texture2D<float> ShadowAtlas;
[[vk::binding(16, 0)]] StructuredBuffer<GpuShadowRecord> ShadowRecords;
[[vk::binding(17, 0)]] StructuredBuffer<GpuShadowView> ShadowViews;
[[vk::binding(0, 1)]] SamplerState Samplers[];
[[vk::binding(1, 1)]] Texture2D<float4> Textures[];

// ForwardPlus::LightShadow.
float ForwardLightShadow(ForwardView view, GpuLightRecord light, float3 position, float3 normal, float3 toLight)
{
	if ((view.Flags & ForwardViewFlagShadows) == 0 || light.ShadowIndex == GpuLightNoShadow || (light.Flags & GpuLightFlagCastsShadows) == 0)
	{
		return 1.0;
	}
	const float cameraDepth = dot(position - view.CameraPosition, view.CameraForward);
	return ShadowFactor(ShadowAtlas, ShadowRecords, ShadowViews, light.ShadowIndex, position, normal, toLight, cameraDepth);
}

// The direct term of ForwardPlus::Shade: directional lights and the cluster's list, shadowed.
float3 ForwardDirect(ForwardView view, ClusterGrid grid, StandardPbrSurface surface, float3 toCamera, float3 position, float pixelX,
	float pixelY, float viewDepth)
{
	const GpuLightHeader header = LightHeader[0];
	const ClusterRecord record = ClusterRecords[ClusterIndexFor(grid, pixelX, pixelY, viewDepth)];
	// One loop over the directional lights, then the cluster's list: a single call site
	// keeps the shadow lookup inlined once (GPU-assisted validation instruments every
	// storage-buffer access and flags shaders with too many).
	float3 sum = float3(0.0, 0.0, 0.0);
	const uint count = header.DirectionalCount + record.Count;
	for (uint i = 0; i < count; ++i)
	{
		const uint row = i < header.DirectionalCount ? i : header.FirstLocalRow + ClusterIndices[record.Offset + (i - header.DirectionalCount)];
		const GpuLightRecord light = Lights[row];
		const GpuLightSample sample = GpuLightEvaluate(light, position);
		sum += StandardPbrEvaluateBrdf(surface.BaseColor, surface.Metallic, surface.PerceptualRoughness, surface.Normal, toCamera,
				   sample.Direction) *
			sample.Radiance * ForwardLightShadow(view, light, position, surface.Normal, sample.Direction);
	}
	return sum;
}

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
	float v[ForwardVertexFloats];
	for (uint i = 0; i < ForwardVertexFloats; ++i)
	{
		v[i] = Vertices[base + i];
	}
	const float3 position = float3(v[0], v[1], v[2]);
	const float3 normal = float3(v[3], v[4], v[5]);
	const float4 tangent = float4(v[6], v[7], v[8], v[9]);
	const float2 uv = float2(v[10], v[11]);

	const float3 world = GpuSceneTransformPoint(current, position);
	const bool mirrored = ForwardDeterminant(current) < 0.0;
	const float4 p = float4(world, 1.0);
	VertexOutput output;
	const float4 clip = ForwardProject(viewProjection, p);
	// TAA jitter (ForwardPlus::BuildForwardViewRecord): an NDC offset, so clip.xy moves by jitter * w.
	output.Position = float4(clip.xy + Views[0].Jitter * clip.w, clip.zw);
	output.WorldPosition = world;
	output.Normal = ForwardTransformNormal(current, normal);
	output.Tangent = float4(ForwardTransformDirection(current, tangent.xyz), (tangent.w < 0.0) != mirrored ? -1.0 : 1.0);
	output.Uv = uv;
	output.Material = instance.MaterialSet < Views[0].MaterialCount ? instance.MaterialSet : 0u;
#if !FORWARD_TRANSPARENT
	output.ObjectId = instance.ObjectId;
	float4 previous[3] = { rows[3], rows[4], rows[5] };
	output.CurrentClip = clip;
	float4 previousViewProjection[4] = { projections[4], projections[5], projections[6], projections[7] };
	output.PreviousClip = ForwardProject(previousViewProjection, float4(GpuSceneTransformPoint(previous, position), 1.0));
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
	float4 Debug;
	bool UseDebug;
};

ShadedPixel ShadePixel(VertexOutput input, bool rasterFrontFacing)
{
	const ForwardView view = Views[0];
	const StandardMaterialParameters material = Materials[input.Material];
	// ForwardPlus::BuildFrame: the rasterizer's facing, corrected for mirroring transforms.
	const bool frontFacing = rasterFrontFacing != (input.Mirrored != 0u);
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
	const StandardPbrSurface surface = StandardPbrResolve(material, texels, normal, tangent, bitangent, frontFacing);
	if (surface.Discard)
	{
		discard;
	}

	const ClusterGrid grid = Grid[0];
	const float3 toCamera = normalize(view.CameraPosition - input.WorldPosition);
	const float viewDepth = -dot(grid.ViewRow2, float4(input.WorldPosition, 1.0));
	const float3 direct = ForwardDirect(view, grid, surface, toCamera, input.WorldPosition, input.Position.x, input.Position.y, viewDepth);
	float3 ibl = float3(0.0, 0.0, 0.0);
	ShadedPixel pixel;
	pixel.Specular = float3(0.0, 0.0, 0.0);
	if ((view.Flags & ForwardViewFlagEnvironment) != 0)
	{
		const StandardPbrEnvironment environment = EnvironmentLookup(EnvironmentIrradiance, EnvironmentPrefiltered, view.PrefilteredMipCount,
			EnvironmentBrdfLut, EnvironmentSampler, view.EnvironmentIntensity, view.EnvironmentRotation, surface, toCamera);
		ibl = StandardPbrEvaluateEnvironment(surface, toCamera, environment);
		pixel.Reflectance = StandardPbrEnvironmentSpecularWeight(surface, toCamera, environment.BrdfScale, environment.BrdfBias);
		pixel.Specular = environment.Prefiltered * pixel.Reflectance;
	}
	else if ((view.Flags & ForwardViewFlagBrdfLut) != 0)
	{
		// A LUT without an environment (item 76): the reflectance screen-space reflections use.
		const float roughness = clamp(surface.PerceptualRoughness, StandardPbrMinPerceptualRoughness, 1.0);
		const float nDotV = clamp(dot(surface.Normal, toCamera), 1.0e-4, 1.0);
		const float2 ab = EnvironmentBrdfLut.SampleLevel(EnvironmentSampler, float2(nDotV, roughness), 0.0).rg;
		pixel.Reflectance = StandardPbrEnvironmentSpecularWeight(surface, toCamera, ab.x, ab.y);
	}
	else
	{
		pixel.Reflectance = float3(0.0, 0.0, 0.0);
	}
	pixel.Indirect = view.Ambient * surface.BaseColor * surface.Occlusion + ibl;
	pixel.Color = float4(direct + pixel.Indirect + surface.Emissive, surface.Alpha);
	pixel.Normal = surface.Normal;
	pixel.Roughness = surface.PerceptualRoughness;
	pixel.UseDebug = view.DebugMode == ForwardDebugClusterHeatmap;
	pixel.Debug = float4(0.0, 0.0, 0.0, 1.0);
	if (pixel.UseDebug)
	{
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
};

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
	return output;
}
#endif
