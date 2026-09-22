#pragma once
#include "Engine/Systems/Renderer/RenderGraph/GraphHandle.h"
#include <vector>

namespace Swim::Render
{
	// GeometryHeap's persistent buffers imported into one graph. Pages[i] matches
	// the page ids in GpuMeshMetadata (invalid for released dedicated pages);
	// Submeshes holds the GpuSubmeshRecord rows referenced by metadata/LODs.
	// Imports return to their resting read states, so draw/cull passes declare
	// VertexBuffer/IndexBuffer/ShaderRead reads against these handles.
	struct GeometryGraphResources
	{
		std::vector<GraphBuffer> Pages;
		GraphBuffer Metadata;
		GraphBuffer Submeshes;
		std::vector<GraphPass> UploadPasses;
		std::uint32_t RecordedMeshes = 0;
		std::uint64_t RecordedBytes = 0;
	};
} // namespace Swim::Render
