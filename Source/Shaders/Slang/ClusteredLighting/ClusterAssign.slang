// Clustered lighting pass 3 (item 65): the per-cluster light bitmasks. One thread per
// (cluster, mask word) tests its 32 lights' view spheres against the cluster's AABB and
// writes the word; nothing is ever truncated, so thousands of lights stay exact. The
// first thread also clears ClusterStats for the summary pass. Mirrors the mask half of
// Clustering::AssignLights; bindings follow ClusterAssignBindings.
#include "ClusteredLighting/ClusterGrid.slang"
#include "Lights/GpuLightRecords.slang"

struct AssignConstants
{
	uint RowThreads; // Threads per dispatch row (the dispatch is 2D past 65535 groups).
	uint Reserved0;
	uint Reserved1;
	uint Reserved2;
};

[[vk::push_constant]] ConstantBuffer<AssignConstants> Constants;
[[vk::binding(0, 0)]] StructuredBuffer<ClusterGrid> Grid;
[[vk::binding(1, 0)]] StructuredBuffer<GpuLightHeader> LightHeader;
[[vk::binding(2, 0)]] StructuredBuffer<ClusterViewLight> ViewLights;
[[vk::binding(3, 0)]] StructuredBuffer<ClusterAabb> Bounds;
[[vk::binding(4, 0)]] RWStructuredBuffer<uint> Indices;
[[vk::binding(5, 0)]] RWStructuredBuffer<ClusterStats> Stats;

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	const uint thread = id.x + id.y * Constants.RowThreads;
	if (thread == 0)
	{
		ClusterStats cleared;
		cleared.VisibleLights = 0;
		cleared.RequestedIndices = 0;
		cleared.WrittenIndices = 0;
		cleared.OverflowClusters = 0;
		cleared.DroppedIndices = 0;
		cleared.MaxRawLightsPerCluster = 0;
		cleared.NonEmptyClusters = 0;
		cleared.ClusterCount = 0;
		Stats[0] = cleared;
	}
	const ClusterGrid grid = Grid[0];
	const uint maskWords = ClusterMaskWords(grid);
	const uint cluster = thread / maskWords;
	if (cluster >= grid.Dimensions.w)
	{
		return;
	}
	const uint word = thread - cluster * maskWords;
	const uint lightCount = LightHeader[0].LocalCount;
	const ClusterAabb box = Bounds[cluster];
	const uint first = word * 32u;
	const uint last = min(first + 32u, lightCount);
	uint mask = 0;
	for (uint i = first; i < last; ++i)
	{
		const ClusterViewLight light = ViewLights[i];
		if (ClusterSphereIntersectsAabb(light.Center, light.Radius, box))
		{
			mask |= 1u << (i - first);
		}
	}
	Indices[cluster * ClusterBlockWords(grid) + ClusterOccupancyWords(grid) + word] = mask;
}
