#pragma once
#include "Engine/Systems/Renderer/RenderGraph/GraphHandle.h"
#include "Engine/Systems/Renderer/Resources/GpuHandle.h"
#include "Engine/Systems/Renderer/Skinning/SkinningRecords.h"

#include <optional>
#include <vector>

namespace Swim::Render
{
	struct SkinnedMeshTag;
	using SkinnedMeshHandle = GpuHandle<SkinnedMeshTag>;
	using SkinInstanceHandle = GpuSkinHandle;

	// One instance skinned by a SkinningSystem::Record.
	struct SkinningFrameInstance
	{
		SkinInstanceHandle Handle;
		GpuSkinDispatch Record;
		std::uint32_t Page = 0; // GeometryHeap vertex page written.
		bool Settling = false;	// Re-skinned with previous = current (no SetPose this frame).
	};

	// What one Record scheduled: the frame's rows (in dispatch order, grouped by
	// output page) and the persistent buffers imported into the graph.
	struct SkinningGraphResources
	{
		std::vector<SkinningFrameInstance> Instances;
		std::optional<GraphBuffer> Dispatches;
		std::optional<GraphBuffer> SourceVertices;
		std::optional<GraphBuffer> SkinVertices;
		std::optional<GraphBuffer> MorphDeltas;
		std::optional<GraphBuffer> Palettes;
		std::optional<GraphBuffer> MorphWeights;
		std::optional<GraphPass> SkinPass;
		std::uint32_t UploadedMeshes = 0;	// Source data uploaded this frame.
		std::uint32_t SkippedInstances = 0; // Output mesh or source not yet in a graph.
	};
} // namespace Swim::Render
