#pragma once
#include "Engine/Systems/Renderer/RenderGraph/GraphHandle.h"

#include <cstdint>
#include <optional>

namespace Swim::Render
{
	// One GpuLightBuffer::Import. Both buffers are ShaderRead after the upload passes:
	// Lights holds RowCount GpuLightRecords (directional rows first, local rows from
	// FirstLocalRow), Header one GpuLightHeader with the live counts.
	struct GpuLightGraphResources
	{
		GraphBuffer Lights;
		GraphBuffer Header;
		std::optional<GraphPass> UploadPass;
		std::optional<GraphPass> HeaderUploadPass;
		std::uint32_t DirectionalCount = 0;
		std::uint32_t LocalCount = 0;
		std::uint32_t FirstLocalRow = 0;
		std::uint32_t RowCount = 0;
	};
} // namespace Swim::Render
