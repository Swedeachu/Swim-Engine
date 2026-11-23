#pragma once
#include "Engine/Systems/Renderer/Geometry/GeometryRangeAllocator.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <cstdint>

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
		// Index pages serve one vertex page only: a draw binds each index page with a single
		// vertex page.
		std::uint32_t Partner = UINT32_MAX;
	};

} // namespace Swim::Render::Internal
