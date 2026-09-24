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
	float4 current[3];
	for (uint row = 0; row < 3; ++row)
	{
		current[row] = Transforms[instance.TransformIndex].Current[row];
	}
	const ForwardView view = Views[0];
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
	output.Position = float4(dot(view.ViewProjection[0], p), dot(view.ViewProjection[1], p), dot(view.ViewProjection[2], p),
		dot(view.ViewProjection[3], p));
	output.WorldPosition = world;
	output.Normal = ForwardTransformNormal(current, normal);
	output.Tangent = float4(ForwardTransformDirection(current, tangent.xyz), (tangent.w < 0.0) != mirrored ? -1.0 : 1.0);
	output.Uv = uv;
	output.Material = instance.MaterialSet < view.MaterialCount ? instance.MaterialSet : 0u;
#if !FORWARD_TRANSPARENT
	output.ObjectId = instance.ObjectId;
#endif
	output.Mirrored = mirrored ? 1u : 0u;
	return output;
}

struct ShadedPixel
{
	float4 Color; // Linear radiance, straight alpha.
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
	if ((view.Flags & ForwardViewFlagEnvironment) != 0)
	{
		const StandardPbrEnvironment environment = EnvironmentLookup(EnvironmentIrradiance, EnvironmentPrefiltered, view.PrefilteredMipCount,
			EnvironmentBrdfLut, EnvironmentSampler, view.EnvironmentIntensity, view.EnvironmentRotation, surface, toCamera);
		ibl = StandardPbrEvaluateEnvironment(surface, toCamera, environment);
	}
	ShadedPixel pixel;
	pixel.Color = float4(direct + view.Ambient * surface.BaseColor * surface.Occlusion + ibl + surface.Emissive, surface.Alpha);
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
[shader("fragment")]
float4 fragmentMain(VertexOutput input, bool frontFacing : SV_IsFrontFace) : SV_Target0
{
	const ShadedPixel pixel = ShadePixel(input, frontFacing);
	return float4(pixel.Color.rgb * pixel.Color.a, pixel.Color.a); // Premultiplied.
}
#else
struct OpaqueOutput
{
	float4 Color : SV_Target0;
	float ObjectId : SV_Target1;
};

[shader("fragment")]
OpaqueOutput fragmentMain(VertexOutput input, bool frontFacing : SV_IsFrontFace)
{
	const ShadedPixel pixel = ShadePixel(input, frontFacing);
	OpaqueOutput output;
	output.Color = pixel.UseDebug ? pixel.Debug : float4(pixel.Color.rgb, 1.0);
	output.ObjectId = float(input.ObjectId + 1u);
	return output;
}
#endif
