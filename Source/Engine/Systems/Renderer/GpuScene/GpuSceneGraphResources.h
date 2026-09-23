#pragma once
#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"

#include <vector>

namespace Swim::Render
{
	// What GpuScene::Import declared in a graph. Instances/Transforms are imported
	// in ShaderRead and remain there after the upload passes, so later passes can
	// read them directly (for example culling). RowCount bounds valid rows.
	struct GpuSceneGraphResources
	{
		GraphBuffer Instances;
		GraphBuffer Transforms;
		std::vector<GraphPass> UploadPasses;
		std::uint32_t RowCount = 0;
		std::uint32_t InstanceRows = 0;
		std::uint32_t TransformRows = 0;
		std::uint32_t UploadRuns = 0;
		std::uint64_t UploadBytes = 0;
	};
} // namespace Swim::Render
