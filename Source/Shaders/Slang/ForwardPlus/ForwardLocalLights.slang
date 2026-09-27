// Deferred local lights (Forward+ with ForwardPlusFrame::DeferLocalLights): the clustered
// point and spot lights of every opaque pixel, added to the colour target in one compute
// pass after the opaque pass (SwimForwardOpaqueDeferred shaded everything else and wrote
// the surface's base colour and metalness). The per-light work is exactly the Forward+
// loop's (ForwardDirect: range test, GpuLightEvaluate, StandardPbrEvaluateBrdf, the shadow
// factor), but it runs once per pixel with no helper lanes of partly covered quads and in
// a small program (high occupancy hides the light loads), where the draw's fragment stage
// carried the whole material and environment evaluation around the loop.
// Bindings follow ForwardLocalLightsBindings.
#include "ClusteredLighting/ClusterGrid.slang"
#include "ForwardPlus/ForwardPlusRecords.slang"
#include "Lights/GpuLightRecords.slang"
#include "Materials/StandardPbr.slang"
#include "Shadows/ShadowRecords.slang"

[[vk::binding(0, 0)]] StructuredBuffer<ForwardView> Views;
[[vk::binding(1, 0)]] StructuredBuffer<GpuLightRecord> Lights;
[[vk::binding(2, 0)]] StructuredBuffer<GpuLightHeader> LightHeader;
[[vk::binding(3, 0)]] StructuredBuffer<ClusterGrid> Grid;
[[vk::binding(4, 0)]] StructuredBuffer<ClusterRecord> ClusterRecords;
[[vk::binding(5, 0)]] StructuredBuffer<uint> ClusterIndices;
[[vk::binding(6, 0)]] Texture2D<float> Depth;
[[vk::binding(7, 0)]] Texture2D<float4> Normal;	 // World shading normal, perceptual roughness.
[[vk::binding(8, 0)]] Texture2D<float4> Material; // Base colour, metalness (< 0: skip the pixel).
[[vk::binding(9, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Color;
[[vk::binding(10, 0)]] Texture2D<float> ShadowAtlas;
[[vk::binding(11, 0)]] StructuredBuffer<GpuShadowRecord> ShadowRecords;
[[vk::binding(12, 0)]] StructuredBuffer<GpuShadowView> ShadowViews;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	const ClusterGrid grid = Grid[0];
	if (id.x >= grid.Limits.x || id.y >= grid.Limits.y)
	{
		return;
	}
	const int3 at = int3(int(id.x), int(id.y), 0);
	const float depth = Depth.Load(at);
	if (!(depth > 0.0))
	{
		return; // No opaque surface (reverse-Z clear).
	}
	const float4 material = Material.Load(at);
	if (material.a < 0.0)
	{
		return; // A debug view.
	}
	const GpuLightHeader header = LightHeader[0];
	const float pixelX = float(id.x) + 0.5;
	const float pixelY = float(id.y) + 0.5;
	const float viewDepth = ClusterViewDepthFromNdc(grid, depth);
	const ClusterRecord record = ClusterRecords[ClusterIndexFor(grid, pixelX, pixelY, viewDepth)];
	if (record.Count == 0)
	{
		return;
	}
	// The surface's view position (as ClusterFrustumBounds maps pixels), then world.
	const float ndcX = pixelX / float(grid.Limits.x) * 2.0 - 1.0;
	const float ndcY = 1.0 - pixelY / float(grid.Limits.y) * 2.0;
	const float3 viewPosition = float3(viewDepth * (ndcX + grid.Projection.z) / grid.Projection.x,
		viewDepth * (ndcY + grid.Projection.w) / grid.Projection.y, -viewDepth);
	const float3 d = viewPosition - float3(grid.ViewRow0.w, grid.ViewRow1.w, grid.ViewRow2.w);
	const float3 position = grid.ViewRow0.xyz * d.x + grid.ViewRow1.xyz * d.y + grid.ViewRow2.xyz * d.z;

	const ForwardView view = Views[0];
	const float4 encoded = Normal.Load(at);
	const float3 normal = normalize(encoded.xyz);
	const float roughness = encoded.w;
	const float3 baseColor = material.rgb;
	const float metallic = material.a;
	const float3 toCamera = normalize(view.CameraPosition - position);
	const bool shadows = (view.Flags & ForwardViewFlagShadows) != 0;
	const float cameraDepth = dot(position - view.CameraPosition, view.CameraForward);

	// Scalarized walk: the union of the wave's cluster masks is walked in the same order by
	// every lane (each evaluates only the lights of its own cluster), so the loop control and
	// the light loads are uniform across the wave even where its pixels span several clusters
	// (depth edges), instead of the lanes diverging through different lists.
	float3 sum = float3(0.0, 0.0, 0.0);
	const uint occupancyWords = ClusterOccupancyWords(grid);
	for (uint o = 0; o < occupancyWords; ++o)
	{
		const uint occupancy = ClusterIndices[record.Offset + o];
		uint waveOccupancy = WaveActiveBitOr(occupancy);
		while (waveOccupancy != 0)
		{
			const uint bit = firstbitlow(waveOccupancy);
			waveOccupancy &= waveOccupancy - 1u;
			const uint word = o * 32u + bit;
			const uint mask = ((occupancy >> bit) & 1u) != 0 ? ClusterIndices[record.Offset + occupancyWords + word] : 0u;
			uint waveMask = WaveActiveBitOr(mask);
			while (waveMask != 0)
			{
				const uint lightBit = firstbitlow(waveMask);
				waveMask &= waveMask - 1u;
				if (((mask >> lightBit) & 1u) == 0)
				{
					continue;
				}
				const GpuLightRecord light = Lights[header.FirstLocalRow + word * 32u + lightBit];
				const float3 delta = light.Position - position;
				if (dot(delta, delta) * light.InverseRangeSquared >= 1.0)
				{
					continue;
				}
				const GpuLightSample sample = GpuLightEvaluate(light, position);
				if (all(sample.Radiance <= float3(0.0, 0.0, 0.0)))
				{
					continue;
				}
				const float3 brdf = StandardPbrEvaluateBrdf(baseColor, metallic, roughness, normal, toCamera, sample.Direction);
				if (all(brdf <= float3(0.0, 0.0, 0.0)))
				{
					continue;
				}
				float shadow = 1.0;
				if (shadows && light.ShadowIndex != GpuLightNoShadow && (light.Flags & GpuLightFlagCastsShadows) != 0)
				{
					shadow = ShadowFactor(ShadowAtlas, ShadowRecords, ShadowViews, light.ShadowIndex, position, normal, sample.Direction,
						cameraDepth);
				}
				sum += brdf * sample.Radiance * shadow;
			}
		}
	}
	if (any(sum > float3(0.0, 0.0, 0.0)))
	{
		const int2 pixel = int2(id.xy);
		Color[pixel] = Color[pixel] + float4(sum, 0.0);
	}
}
