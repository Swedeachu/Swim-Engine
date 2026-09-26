#pragma once
#include "Engine/Systems/Renderer/ClusteredLighting/ClusterBindings.h"
#include "Engine/Systems/Renderer/ClusteredLighting/ClusterGraphResources.h"
#include "Engine/Systems/Renderer/Lights/GpuLightGraphResources.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"

#include <string>

namespace Swim::Render
{
	// One compiled clustering program.
	struct ClusterProgram
	{
		Rhi::ComputePipeline* Pipeline = nullptr;
		Rhi::PipelineLayout* Layout = nullptr;
		std::uint32_t Space = 0;
	};

	struct ClusteredLightAssignerDesc
	{
		ClusterProgram Cull;	// ClusterLightCull.slang
		ClusterProgram Bounds;	// ClusterBounds.slang
		ClusterProgram Assign;	// ClusterAssign.slang (bitmask words)
		ClusterProgram Scan;	// ClusterScan.slang (per-cluster summary)
		ClusterProgram Heatmap; // ClusterHeatmap.slang; optional (RecordHeatmap throws without it).
		std::string DebugName = "Clustered lights";
	};

	// GPU clustered light assignment (critical-path items 64, 65 and 68). Per frame
	// and view, Record schedules four compute passes over a GpuLightBuffer import:
	//   1. cull: each local light's bounding sphere to view space, culled against
	//      the clustered volume;
	//   2. bounds: each cluster's view-space AABB (ClusterGrid tiles x log slices);
	//   3. masks: per (cluster, 32-light word), the bitmask of lights whose spheres
	//      touch the cluster (never truncated, so any number of lights up to the
	//      grid's LightCapacity stays exact: no dropped lights, no tile seams);
	//   4. summary: per cluster, the occupancy words (which mask words hold lights),
	//      the record (block offset, light count) and ClusterStats.
	// Every pass is deterministic, so Clustering::AssignLights reproduces the result
	// exactly. Directional lights stay outside the clusters; shaders loop them
	// separately (ClusteredLighting.slang).
	class ClusteredLightAssigner
	{
	  public:
		// Throws std::invalid_argument when a required program is missing.
		explicit ClusteredLightAssigner(ClusteredLightAssignerDesc desc);

		// The grid desc and view are validated by MakeClusterGridRecord.
		ClusterGraphResources Record(
			RenderGraph& graph, const GpuLightGraphResources& lights, const ClusterGridDesc& grid, const ClusterView& view) const;

		// The debug heatmap (item 68): a viewport-sized RGBA8Unorm texture coloring
		// each pixel by its cluster's light count (Clustering::HeatmapPixel). The depth
		// must be a sampled single-sample 2D D32Float or R32Float texture of the viewport size.
		GraphTexture RecordHeatmap(RenderGraph& graph, const ClusterGraphResources& clusters, GraphTexture depth) const;

		bool HasHeatmap() const { return desc.Heatmap.Pipeline != nullptr; }

	  private:
		ClusteredLightAssignerDesc desc;
	};
} // namespace Swim::Render
