#pragma once
#include "Engine/Systems/Renderer/Geometry/GeometryRangeAllocator.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

namespace Swim::Render::Internal
{
	enum class GeometryStream : std::uint8_t
	{
		Vertex,
		Index,
		Meshlet
	};

	struct GeometryPage
	{
		GeometryStream Stream = GeometryStream::Vertex;
		bool Dedicated = false;
		std::unique_ptr<Rhi::Buffer> Buffer; // Null after a dedicated page retires.
		std::optional<GeometryRangeAllocator> Allocator;
	};
} // namespace Swim::Render::Internal
