// Clustered lighting pass 4 (item 65): one 256-thread group turns per-cluster
// counts into compact offsets (exclusive prefix sum in cluster order), clamps
// them to IndexCapacity and writes ClusterStats. Mirrors the second half of
// Clustering::AssignLights; bindings follow ClusterScanBindings.
#include "ClusteredLighting/ClusterGrid.slang"
#include "Lights/GpuLightRecords.slang"

[[vk::binding(0, 0)]] StructuredBuffer<ClusterGrid> Grid;
[[vk::binding(1, 0)]] StructuredBuffer<GpuLightHeader> LightHeader;
[[vk::binding(2, 0)]] StructuredBuffer<ClusterViewLight> ViewLights;
[[vk::binding(3, 0)]] RWStructuredBuffer<ClusterRecord> Records;
[[vk::binding(4, 0)]] RWStructuredBuffer<ClusterStats> Stats;

static const uint Threads = 256;
groupshared uint SharedSums[Threads];
groupshared uint SharedMax[Threads];
groupshared uint SharedOverflow[Threads];
groupshared uint SharedVisible[Threads];
groupshared uint SharedWritten[Threads];
groupshared uint SharedNonEmpty[Threads];

[shader("compute")]
[numthreads(256, 1, 1)]
void computeMain(uint3 local : SV_GroupThreadID)
{
	const uint thread = local.x;
	const ClusterGrid grid = Grid[0];
	const uint clusterCount = grid.Dimensions.w;
	const uint capacity = grid.Limits.w;
	const uint maxPerCluster = grid.Limits.z;
	// Each thread owns a contiguous run of clusters.
	const uint perThread = (clusterCount + Threads - 1) / Threads;
	const uint begin = min(thread * perThread, clusterCount);
	const uint end = min(begin + perThread, clusterCount);
	uint sum = 0;
	uint largest = 0;
	uint overflow = 0;
	for (uint c = begin; c < end; ++c)
	{
		const ClusterRecord record = Records[c];
		sum += record.Count;
		largest = max(largest, record.RawCount);
		overflow += record.RawCount > maxPerCluster ? 1 : 0;
	}
	uint visible = 0;
	const uint lightCount = LightHeader[0].LocalCount;
	for (uint i = thread; i < lightCount; i += Threads)
	{
		visible += ViewLights[i].Radius >= 0.0 ? 1 : 0;
	}
	SharedSums[thread] = sum;
	SharedMax[thread] = largest;
	SharedOverflow[thread] = overflow;
	SharedVisible[thread] = visible;
	GroupMemoryBarrierWithGroupSync();
	// Inclusive Hillis-Steele scan of the per-thread sums; reductions of the rest.
	for (uint stride = 1; stride < Threads; stride *= 2)
	{
		const uint add = thread >= stride ? SharedSums[thread - stride] : 0;
		GroupMemoryBarrierWithGroupSync();
		SharedSums[thread] += add;
		GroupMemoryBarrierWithGroupSync();
	}
	for (uint half = Threads / 2; half > 0; half /= 2)
	{
		if (thread < half)
		{
			SharedMax[thread] = max(SharedMax[thread], SharedMax[thread + half]);
			SharedOverflow[thread] += SharedOverflow[thread + half];
			SharedVisible[thread] += SharedVisible[thread + half];
		}
		GroupMemoryBarrierWithGroupSync();
	}
	const uint requested = SharedSums[Threads - 1];
	const uint maxRaw = SharedMax[0];
	const uint overflowClusters = SharedOverflow[0];
	const uint visibleLights = SharedVisible[0];
	// Write this thread's offsets (exclusive prefix, clamped to the capacity).
	uint prefix = SharedSums[thread] - sum;
	uint written = 0;
	uint nonEmpty = 0;
	for (uint c = begin; c < end; ++c)
	{
		ClusterRecord record = Records[c];
		const uint count = record.Count;
		record.Offset = min(prefix, capacity);
		record.Count = min(count, capacity - record.Offset);
		prefix += count;
		written += record.Count;
		nonEmpty += record.Count > 0 ? 1 : 0;
		Records[c] = record;
	}
	GroupMemoryBarrierWithGroupSync();
	SharedWritten[thread] = written;
	SharedNonEmpty[thread] = nonEmpty;
	GroupMemoryBarrierWithGroupSync();
	for (uint half = Threads / 2; half > 0; half /= 2)
	{
		if (thread < half)
		{
			SharedWritten[thread] += SharedWritten[thread + half];
			SharedNonEmpty[thread] += SharedNonEmpty[thread + half];
		}
		GroupMemoryBarrierWithGroupSync();
	}
	if (thread == 0)
	{
		ClusterStats stats;
		stats.VisibleLights = visibleLights;
		stats.RequestedIndices = requested;
		stats.WrittenIndices = SharedWritten[0];
		stats.OverflowClusters = overflowClusters;
		stats.DroppedIndices = requested - SharedWritten[0];
		stats.MaxRawLightsPerCluster = maxRaw;
		stats.NonEmptyClusters = SharedNonEmpty[0];
		stats.ClusterCount = clusterCount;
		Stats[0] = stats;
	}
}
