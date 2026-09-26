#pragma once
#include "Engine/Systems/Renderer/ClusteredLighting/ClusterGrid.h"
#include "Engine/Systems/Renderer/RenderGraph/GraphHandle.h"

#include <vector>

namespace Swim::Render
{
	// One ClusteredLightAssigner::Record. Every buffer is transient and ShaderRead
	// once the passes have run: shading reads Grid, Records and Indices (plus the
	// light buffer); ViewLights, Bounds and Stats serve diagnostics and tests.
	struct ClusterGraphResources
	{
		GraphBuffer Grid;		// One ClusterGridRecord.
		GraphBuffer ViewLights; // One ViewLight per local light row.
		GraphBuffer Bounds;		// One view-space AABB (2 x float4) per cluster.
		GraphBuffer Records;	// One ClusterRecord per cluster.
		GraphBuffer Indices;	// ClusterCount * ClusterBlockWords uints: occupancy + light bitmask words.
		GraphBuffer Stats;		// One ClusterStats.
		ClusterGridRecord GridRecord;
		ClusterGridLayout Layout;
		std::uint32_t LocalLightCapacity = 0;
		std::vector<GraphPass> Passes; // Cull, bounds, masks, summary.
	};
} // namespace Swim::Render
