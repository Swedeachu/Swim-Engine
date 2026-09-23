// Clustered lighting passes 3 and 5 (item 65). One thread per cluster walks every
// local light in index order, 64 at a time through groupshared memory.
//  - Count mode: RawCount = lights whose view sphere touches the cluster's AABB,
//    Count = min(RawCount, MaxLightsPerCluster).
//  - Write mode (after ClusterScan fixed Offset and Count): the first Count hits'
//    local indices at Offset.
// Mirrors Clustering::AssignLights; bindings follow ClusterAssignBindings.
#include "ClusteredLighting/ClusterGrid.slang"
#include "Lights/GpuLightRecords.slang"

struct AssignConstants
{
	uint Mode; // 0 count, 1 write.
	uint Reserved0;
	uint Reserved1;
	uint Reserved2;
};

[[vk::push_constant]] ConstantBuffer<AssignConstants> Constants;
[[vk::binding(0, 0)]] StructuredBuffer<ClusterGrid> Grid;
[[vk::binding(1, 0)]] StructuredBuffer<GpuLightHeader> LightHeader;
[[vk::binding(2, 0)]] StructuredBuffer<ClusterViewLight> ViewLights;
[[vk::binding(3, 0)]] StructuredBuffer<ClusterAabb> Bounds;
[[vk::binding(4, 0)]] RWStructuredBuffer<ClusterRecord> Records;
[[vk::binding(5, 0)]] RWStructuredBuffer<uint> Indices;

static const uint Batch = 64;
groupshared float4 SharedLights[Batch];

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID, uint3 local : SV_GroupThreadID)
{
	const ClusterGrid grid = Grid[0];
	const uint lightCount = LightHeader[0].LocalCount;
	const bool active = id.x < grid.Dimensions.w;
	const uint cluster = active ? id.x : 0;
	const ClusterAabb box = Bounds[cluster];
	const bool writing = Constants.Mode != 0;
	ClusterRecord record = Records[cluster]; // Offset and Count, when writing.
	uint raw = 0;
	uint written = 0;
	for (uint first = 0; first < lightCount; first += Batch)
	{
		const uint loadIndex = first + local.x;
		if (loadIndex < lightCount)
		{
			const ClusterViewLight light = ViewLights[loadIndex];
			SharedLights[local.x] = float4(light.Center, light.Radius);
		}
		GroupMemoryBarrierWithGroupSync();
		const uint batchCount = min(Batch, lightCount - first);
		for (uint i = 0; i < batchCount; ++i)
		{
			const float4 light = SharedLights[i];
			if (active && ClusterSphereIntersectsAabb(light.xyz, light.w, box))
			{
				if (writing)
				{
					if (written < record.Count)
					{
						Indices[record.Offset + written] = first + i;
						++written;
					}
				}
				else
				{
					++raw;
				}
			}
		}
		GroupMemoryBarrierWithGroupSync();
	}
	if (active && !writing)
	{
		ClusterRecord counted;
		counted.Offset = 0;
		counted.Count = min(raw, grid.Limits.z);
		counted.RawCount = raw;
		counted.Reserved = 0;
		Records[cluster] = counted;
	}
}
