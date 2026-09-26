// Clustered lighting pass 4 (item 65): the per-cluster summary. One thread per cluster
// sets its occupancy words (which mask words hold lights), its record (Offset = the
// cluster's block, Count = RawCount = its light count) and folds ClusterStats with
// atomics; threads also count the visible lights. Mirrors the summary half of
// Clustering::AssignLights; bindings follow ClusterScanBindings.
#include "ClusteredLighting/ClusterGrid.slang"
#include "Lights/GpuLightRecords.slang"

[[vk::binding(0, 0)]] StructuredBuffer<ClusterGrid> Grid;
[[vk::binding(1, 0)]] StructuredBuffer<GpuLightHeader> LightHeader;
[[vk::binding(2, 0)]] StructuredBuffer<ClusterViewLight> ViewLights;
[[vk::binding(3, 0)]] RWStructuredBuffer<ClusterRecord> Records;
[[vk::binding(4, 0)]] RWStructuredBuffer<ClusterStats> Stats;
[[vk::binding(5, 0)]] RWStructuredBuffer<uint> Indices;

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	const ClusterGrid grid = Grid[0];
	const uint thread = id.x;
	if (thread < LightHeader[0].LocalCount && ViewLights[thread].Radius >= 0.0)
	{
		InterlockedAdd(Stats[0].VisibleLights, 1u);
	}
	const uint clusterCount = grid.Dimensions.w;
	if (thread >= clusterCount)
	{
		return;
	}
	const uint maskWords = ClusterMaskWords(grid);
	const uint occupancyWords = ClusterOccupancyWords(grid);
	const uint base = thread * ClusterBlockWords(grid);
	uint count = 0;
	for (uint o = 0; o < occupancyWords; ++o)
	{
		uint occupancy = 0;
		const uint end = min(maskWords, (o + 1u) * 32u);
		for (uint w = o * 32u; w < end; ++w)
		{
			const uint mask = Indices[base + occupancyWords + w];
			if (mask != 0)
			{
				occupancy |= 1u << (w - o * 32u);
				count += countbits(mask);
			}
		}
		Indices[base + o] = occupancy;
	}
	ClusterRecord record;
	record.Offset = base;
	record.Count = count;
	record.RawCount = count;
	record.Reserved = 0;
	Records[thread] = record;
	if (count != 0)
	{
		InterlockedAdd(Stats[0].RequestedIndices, count);
		InterlockedAdd(Stats[0].WrittenIndices, count);
		InterlockedAdd(Stats[0].NonEmptyClusters, 1u);
		InterlockedMax(Stats[0].MaxRawLightsPerCluster, count);
		if (count > grid.Limits.z)
		{
			InterlockedAdd(Stats[0].OverflowClusters, 1u);
		}
	}
	if (thread == 0)
	{
		Stats[0].ClusterCount = clusterCount;
	}
}
