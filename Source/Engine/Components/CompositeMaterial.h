#pragma once

#include "Engine/Systems/Renderer/Core/Material/LegacyRenderBinding.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Engine
{

	// Transitional collection of independent mesh/material draw bindings for a
	// complex model. The final path is ModelAsset nodes + material-slot handles.
	struct CompositeMaterial
	{
		std::vector<std::shared_ptr<LegacyRenderBinding>> subMaterials;
		std::string filePath;

		CompositeMaterial() = default;
		explicit CompositeMaterial(std::vector<std::shared_ptr<LegacyRenderBinding>> data, const std::string& filePath = "")
			: subMaterials(std::move(data)), filePath(filePath)
		{}
	};

}
