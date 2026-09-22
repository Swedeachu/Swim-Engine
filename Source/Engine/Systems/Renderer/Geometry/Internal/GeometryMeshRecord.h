#pragma once
#include "Engine/Systems/Renderer/Geometry/GeometryResidency.h"
#include "Engine/Systems/Renderer/Geometry/Internal/GeometryAllocation.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"
#include <memory>
#include <vector>

namespace Swim::Render::Internal
{
	using GeometryPayload = std::shared_ptr<const std::vector<std::byte>>;

	struct GeometryMeshRecord
	{
		GeometryAllocation Vertex;
		GeometryAllocation Index;
		GeometryAllocation Meshlet;
		GeometryRange Submeshes{}; // Rows in the submesh buffer; Size zero when none.
		GeometryResidency State = GeometryResidency::PendingUpload;
		Rhi::TimelinePoint Upload{};
		// CPU copies retained until their upload submission is committed.
		GeometryPayload VertexBytes;
		GeometryPayload IndexBytes;
		GeometryPayload MeshletBytes;
	};
} // namespace Swim::Render::Internal
