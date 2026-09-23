// Clustered lighting debug heatmap (item 68): each pixel's cluster (from the depth
// buffer) colored by its light count; magenta where the list was truncated,
// transparent past Far. Mirrors Clustering::HeatmapPixel; bindings follow
// ClusterHeatmapBindings.
#include "ClusteredLighting/ClusterGrid.slang"

[[vk::binding(0, 0)]] StructuredBuffer<ClusterGrid> Grid;
[[vk::binding(1, 0)]] StructuredBuffer<ClusterRecord> Records;
[[vk::binding(2, 0)]] Texture2D<float> Depth;
[[vk::binding(3, 0)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> Output;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	const ClusterGrid grid = Grid[0];
	if (id.x >= grid.Limits.x || id.y >= grid.Limits.y)
	{
		return;
	}
	const float depth = ClusterViewDepthFromNdc(grid, Depth.Load(int3(int(id.x), int(id.y), 0)));
	float4 color = float4(0.0, 0.0, 0.0, 0.0);
	if (depth <= grid.DepthParams.w)
	{
		const ClusterRecord record = Records[ClusterIndexFor(grid, float(id.x) + 0.5, float(id.y) + 0.5, depth)];
		color = ClusterHeatmapColor(record.Count, record.RawCount, grid.Limits.z);
	}
	Output[id.xy] = color;
}
