// The Forward+ frame as a draw sees it: the space-0 bindings (ForwardPlusDrawBindings),
// the bindless space 1, and the lighting of a resolved surface. ClusteredForward.slang
// shades with it, and so do render features that draw into the depth prepass, opaque and
// transparent passes (RenderFeature::DrawGeometry): such a program includes this file,
// declares its own resources in space 2 and writes ForwardOpaqueTargets or
// ForwardTransparentTargets. The names declared here are reserved in those programs.
#ifndef SWIM_FORWARD_FRAME_SLANG
#define SWIM_FORWARD_FRAME_SLANG
#include "ClusteredLighting/ClusteredLighting.slang"
#include "Environment/EnvironmentLighting.slang"
#include "ForwardPlus/ForwardPlusRecords.slang"
#include "GpuScene/GpuSceneRecords.slang"
#include "GpuScene/VisibilityRecords.slang"
#include "Shadows/ShadowRecords.slang"

// ClusteredForward's deferred variant leaves the local lights to a later pass.
#ifndef FORWARD_DEFERRED_LOCAL
#define FORWARD_DEFERRED_LOCAL 0
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
	// Local lights come from the cluster's bitmask: occupancy words name the non-zero mask
	// words, and each set bit is a light (increasing index order, never truncated).
	float3 sum = float3(0.0, 0.0, 0.0);
	const uint occupancyWords = (FORWARD_DEFERRED_LOCAL == 0 && record.Count != 0) ? ClusterOccupancyWords(grid) : 0u;
	uint nextOccupancy = 0;
	uint occupancy = 0;
	uint occupancyBase = 0;
	uint mask = 0;
	uint maskBase = 0;
	uint directional = 0;
	[loop]
	for (;;)
	{
		uint row;
		if (directional < header.DirectionalCount)
		{
			row = directional;
			++directional;
		}
		else
		{
			if (mask == 0)
			{
				while (occupancy == 0 && nextOccupancy < occupancyWords)
				{
					occupancy = ClusterIndices[record.Offset + nextOccupancy];
					occupancyBase = nextOccupancy * 32u;
					++nextOccupancy;
				}
				if (occupancy == 0)
				{
					break;
				}
				const uint word = occupancyBase + firstbitlow(occupancy);
				occupancy &= occupancy - 1u;
				mask = ClusterIndices[record.Offset + occupancyWords + word];
				maskBase = word * 32u;
			}
			row = header.FirstLocalRow + maskBase + firstbitlow(mask);
			mask &= mask - 1u;
		}
		const GpuLightRecord light = Lights[row];
		// Clusters are conservative: a local light out of range contributes exactly zero, so
		// it is skipped before its direction, spot cone, BRDF and shadow are evaluated.
		if (light.Type != GpuLightTypeDirectional)
		{
			const float3 delta = light.Position - position;
			if (dot(delta, delta) * light.InverseRangeSquared >= 1.0)
			{
				continue;
			}
		}
		const GpuLightSample sample = GpuLightEvaluate(light, position);
		if (all(sample.Radiance <= float3(0.0, 0.0, 0.0)))
		{
			continue;
		}
		const float3 brdf = StandardPbrEvaluateBrdf(surface.BaseColor, surface.Metallic, surface.PerceptualRoughness, surface.Normal,
			toCamera, sample.Direction);
		// Facing away (zero BRDF): no shadow lookup (its PCF taps are the costliest part).
		if (all(brdf <= float3(0.0, 0.0, 0.0)))
		{
			continue;
		}
		sum += brdf * sample.Radiance * ForwardLightShadow(view, light, position, surface.Normal, sample.Direction);
	}
	return sum;
}

// A surface lit by the frame: every light, ambient, image-based lighting and emission.
struct ForwardLighting
{
	float3 Radiance;	// Linear, everything below plus the direct light.
	float3 Indirect;	// Ambient + IBL (ForwardPlus::IndirectRadiance).
	float3 Reflectance; // Split-sum specular reflectance (ForwardPlus::SpecularReflectance).
	float3 Specular;	// Specular IBL (ForwardPlus::SpecularRadiance), part of Indirect.
};

// pixel: SV_Position.xy of the fragment (its cluster). view and grid: Views[0], Grid[0]
// (a caller that reads them already passes its copies: fewer buffer loads).
ForwardLighting ForwardLightSurface(ForwardView view, ClusterGrid grid, StandardPbrSurface surface, float3 worldPosition, float2 pixel)
{
	const float3 toCamera = normalize(view.CameraPosition - worldPosition);
	StandardPbrStabilizeThin(surface, toCamera);
	const float viewDepth = -dot(grid.ViewRow2, float4(worldPosition, 1.0));
	const float3 direct = ForwardDirect(view, grid, surface, toCamera, worldPosition, pixel.x, pixel.y, viewDepth);
	float3 ibl = float3(0.0, 0.0, 0.0);
	ForwardLighting lit;
	lit.Specular = float3(0.0, 0.0, 0.0);
	if ((view.Flags & ForwardViewFlagEnvironment) != 0)
	{
		const StandardPbrEnvironment environment = EnvironmentLookup(EnvironmentIrradiance, EnvironmentPrefiltered, view.PrefilteredMipCount,
			EnvironmentBrdfLut, EnvironmentSampler, view.EnvironmentIntensity, view.EnvironmentRotation, surface, toCamera);
		ibl = StandardPbrEvaluateEnvironment(surface, toCamera, environment);
		lit.Reflectance = StandardPbrEnvironmentSpecularWeight(surface, toCamera, environment.BrdfScale, environment.BrdfBias);
		lit.Specular = environment.Prefiltered * lit.Reflectance;
	}
	else if ((view.Flags & ForwardViewFlagBrdfLut) != 0)
	{
		// A LUT without an environment (item 76): the reflectance screen-space reflections use.
		const float roughness = clamp(surface.PerceptualRoughness, StandardPbrMinPerceptualRoughness, 1.0);
		const float nDotV = clamp(dot(surface.Normal, toCamera), 1.0e-4, 1.0);
		const float2 ab = EnvironmentBrdfLut.SampleLevel(EnvironmentSampler, float2(nDotV, roughness), 0.0).rg;
		lit.Reflectance = StandardPbrEnvironmentSpecularWeight(surface, toCamera, ab.x, ab.y);
	}
	else
	{
		lit.Reflectance = float3(0.0, 0.0, 0.0);
	}
	lit.Indirect = view.Ambient * surface.BaseColor * surface.Occlusion + ibl;
	lit.Radiance = direct + lit.Indirect + surface.Emissive;
	return lit;
}

ForwardLighting ForwardLightSurface(StandardPbrSurface surface, float3 worldPosition, float2 pixel)
{
	return ForwardLightSurface(Views[0], Grid[0], surface, worldPosition, pixel);
}

// A world position through this frame's camera: the jittered position to rasterize and
// the unjittered clip positions of this frame and the previous one (motion vectors).
// previousWorld is the same point last frame (world for static geometry). The depth
// prepass and the prepassed opaque pass must compute bit-identical depths, so both use
// this function with the same inputs.
struct ForwardClip
{
	float4 Position;
	float4 Current;
	float4 Previous;
};

ForwardClip ForwardProjectWorld(float3 world, float3 previousWorld)
{
	float4 projections[8];
	for (uint row = 0; row < 8; ++row)
	{
		projections[row] = row < 4 ? Views[0].ViewProjection[row] : Views[0].PreviousViewProjection[row - 4];
	}
	float4 viewProjection[4] = { projections[0], projections[1], projections[2], projections[3] };
	float4 previousViewProjection[4] = { projections[4], projections[5], projections[6], projections[7] };
	ForwardClip clip;
	precise const float4 current = ForwardProject(viewProjection, float4(world, 1.0));
	precise const float4 jittered = float4(current.xy + Views[0].JitterOverlay.xy * current.w, current.zw);
	clip.Position = jittered;
	clip.Current = current;
	clip.Previous = ForwardProject(previousViewProjection, float4(previousWorld, 1.0));
	return clip;
}

// An object id for geometry that is no scene object (the target then holds 0).
static const uint ForwardNoObject = 0xFFFFFFFFu;

// The seven targets of the opaque pass (ForwardPlusTargets).
struct ForwardOpaqueTargets
{
	float4 Color : SV_Target0;
	float ObjectId : SV_Target1;	 // Object id + 1 (0: none).
	float2 Velocity : SV_Target2;	 // UV of this frame minus UV of the previous.
	float4 Normal : SV_Target3;		 // World shading normal, perceptual roughness.
	float4 Indirect : SV_Target4;	 // Ambient + IBL radiance.
	float4 Reflectance : SV_Target5; // Split-sum specular reflectance.
	float4 Specular : SV_Target6;	 // Specular IBL radiance (part of Indirect).
};

ForwardOpaqueTargets ForwardOpaqueWrite(ForwardLighting lit, StandardPbrSurface surface, uint objectId, float4 currentClip,
	float4 previousClip)
{
	ForwardOpaqueTargets output;
	output.Color = float4(lit.Radiance, 1.0);
	output.ObjectId = float(objectId + 1u);
	const float2 now = currentClip.xy / currentClip.w;
	const float2 before = previousClip.xy / previousClip.w;
	output.Velocity = (now - before) * float2(0.5, -0.5);
	output.Normal = float4(surface.Normal, surface.PerceptualRoughness);
	output.Indirect = float4(lit.Indirect, 1.0);
	output.Reflectance = float4(lit.Reflectance, 1.0);
	output.Specular = float4(lit.Specular, 1.0);
	return output;
}

// The four targets of the transparent pass: premultiplied colour, blended One /
// OneMinusSourceAlpha over the opaque frame.
struct ForwardTransparentTargets
{
	float4 Color : SV_Target0;
	float4 Indirect : SV_Target1;
	float4 Reflectance : SV_Target2;
	float4 Specular : SV_Target3;
};

ForwardTransparentTargets ForwardTransparentWrite(ForwardLighting lit, float alpha)
{
	ForwardTransparentTargets output;
	output.Color = float4(lit.Radiance * alpha, alpha);
	output.Indirect = float4(0.0, 0.0, 0.0, alpha);
	output.Reflectance = output.Indirect;
	output.Specular = output.Indirect;
	return output;
}

#endif
