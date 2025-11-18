#pragma once

#include "Engine/Systems/Renderer/Core/Material/MaterialData.h"

namespace Engine
{

	// A component to give each entity for which shared material datas to use at render time when using a complex model that is composed of many meshes with materials.
	struct CompositeMaterial
	{

		std::vector<std::shared_ptr<MaterialData>> subMaterials;
		std::string filePath; // the path this mesh was loaded from

		CompositeMaterial() = default;
		explicit CompositeMaterial(std::vector<std::shared_ptr<MaterialData>> data, const std::string& filePath = "")
			: subMaterials(std::move(data)), filePath(filePath)
		{}

	};

}
