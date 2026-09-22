#pragma once
#include "Engine/Systems/Renderer/Geometry/GeometryRange.h"
#include "Engine/Systems/Renderer/Geometry/GpuMeshMetadata.h"

namespace Swim::Render::Internal
{
	struct GeometryAllocation
	{
		std::uint32_t Page = GpuMeshMetadata::InvalidPage;
		GeometryRange Range{};

		bool IsValid() const { return Page != GpuMeshMetadata::InvalidPage; }
	};
} // namespace Swim::Render::Internal
