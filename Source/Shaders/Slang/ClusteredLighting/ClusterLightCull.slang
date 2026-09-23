// Clustered lighting pass 1 (item 65): each local light's bounding sphere in view
// space, culled (radius -1) when it misses the clustered volume. Mirrors
// Clustering::CullLight; bindings follow ClusterLightCullBindings.
#include "ClusteredLighting/ClusterGrid.slang"
#include "Lights/GpuLightRecords.slang"

[[vk::binding(0, 0)]] StructuredBuffer<ClusterGrid> Grid;
[[vk::binding(1, 0)]] StructuredBuffer<GpuLightRecord> Lights;
[[vk::binding(2, 0)]] StructuredBuffer<GpuLightHeader> LightHeader;
[[vk::binding(3, 0)]] RWStructuredBuffer<ClusterViewLight> ViewLights;

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	const GpuLightHeader header = LightHeader[0];
	if (id.x >= header.LocalCount)
	{
		return;
	}
	const ClusterGrid grid = Grid[0];
	const float4 sphere = GpuLightBoundingSphere(Lights[header.FirstLocalRow + id.x]);
	ClusterViewLight light;
	light.Center = ClusterWorldToView(grid, sphere.xyz);
	light.Radius = ClusterSphereIntersectsAabb(light.Center, sphere.w, ClusterVolumeBounds(grid)) ? sphere.w : -1.0;
	ViewLights[id.x] = light;
}
