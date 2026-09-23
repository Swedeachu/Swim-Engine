#pragma once
#include "Engine/Systems/Renderer/RenderGraph/GraphHandle.h"

#include <cstdint>
#include <optional>

namespace Swim::Render
{
	// One GpuMaterialTable::Import: the material buffer (ShaderRead after the upload
	// pass) holds MaterialCount records of RecordSize bytes. Shaders index it with
	// GpuInstanceRecord::MaterialSet and use row 0 (the fallback) for out-of-range
	// values.
	struct GpuMaterialGraphResources
	{
		GraphBuffer Materials;
		std::optional<GraphPass> UploadPass;
		std::uint32_t MaterialCount = 0;
		std::uint32_t RecordSize = 0;
	};
} // namespace Swim::Render
