#pragma once
#include "Engine/Systems/Renderer/Materials/MaterialTemplate.h"

#include <cstdint>
#include <memory>
#include <string>

namespace Swim::Render
{
	struct GpuMaterialTableDesc
	{
		// Every registered instance must use exactly this template.
		std::shared_ptr<const MaterialTemplate> Template;
		// Rows, including the permanent fallback row 0 (live + retiring materials).
		std::uint32_t Capacity = 1024;
		std::string DebugName = "GPU materials";
	};

	struct GpuMaterialTableStats
	{
		std::uint32_t Capacity = 0;
		std::uint32_t RecordSize = 0;
		std::uint32_t LiveMaterials = 0; // Excluding the fallback.
		std::uint32_t RetiringMaterials = 0;
		std::uint32_t DirtyRows = 0;	  // Waiting for the next Import.
		std::uint32_t LastUploadRows = 0; // Rows recorded by the last Import.
		std::uint32_t LastUploadRuns = 0;
		std::uint64_t LastUploadBytes = 0;
	};
} // namespace Swim::Render
