// Clustered Forward+ shading (items 65-66): every directional light plus the local
// lights of the pixel's cluster. Mirrors Clustering::ShadeClustered; without
// truncation it equals the brute-force sum over the whole light buffer.
#ifndef SWIM_CLUSTERED_LIGHTING_SLANG
#define SWIM_CLUSTERED_LIGHTING_SLANG
#include "ClusteredLighting/ClusterGrid.slang"
#include "Lights/GpuLightRecords.slang"
#include "Materials/StandardPbr.slang"

float3 ClusteredShade(ClusterGrid grid, StructuredBuffer<GpuLightRecord> lights, GpuLightHeader header,
	StructuredBuffer<ClusterRecord> records, StructuredBuffer<uint> indices, float3 baseColor, float metallic, float roughness,
	float3 normal, float3 view, float3 worldPosition, float pixelX, float pixelY, float viewDepth)
{
	float3 sum = float3(0.0, 0.0, 0.0);
	for (uint i = 0; i < header.DirectionalCount; ++i)
	{
		const GpuLightSample light = GpuLightEvaluate(lights[i], worldPosition);
		sum += StandardPbrEvaluateBrdf(baseColor, metallic, roughness, normal, view, light.Direction) * light.Radiance;
	}
	const ClusterRecord record = records[ClusterIndexFor(grid, pixelX, pixelY, viewDepth)];
	for (uint i = 0; i < record.Count; ++i)
	{
		const GpuLightSample light = GpuLightEvaluate(lights[header.FirstLocalRow + indices[record.Offset + i]], worldPosition);
		sum += StandardPbrEvaluateBrdf(baseColor, metallic, roughness, normal, view, light.Direction) * light.Radiance;
	}
	return sum;
}

#endif
