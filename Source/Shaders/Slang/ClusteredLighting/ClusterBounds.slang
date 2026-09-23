// Clustered lighting pass 2 (item 64): each cluster's view-space AABB. Mirrors
// Clustering::ClusterBounds; bindings follow ClusterBoundsBindings.
#include "ClusteredLighting/ClusterGrid.slang"

[[vk::binding(0, 0)]] StructuredBuffer<ClusterGrid> Grid;
[[vk::binding(1, 0)]] RWStructuredBuffer<ClusterAabb> Bounds;

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	const ClusterGrid grid = Grid[0];
	if (id.x >= grid.Dimensions.w)
	{
		return;
	}
	Bounds[id.x] = ClusterBounds(grid, id.x);
}
