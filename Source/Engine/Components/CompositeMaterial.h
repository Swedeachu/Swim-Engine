#pragma once

#include "Engine/Systems/Renderer/Core/Material/MaterialData.h"

namespace Engine
{

	// A component to give each entity for which shared material datas to use at render time when using a complex model that is composed of many meshes with materials.
	struct CompositeMaterial
	{

		std::vector<std::shared_ptr<MaterialData>> subMaterials;

		CompositeMaterial() = default;
		explicit CompositeMaterial(std::vector<std::shared_ptr<MaterialData>> data)
			: subMaterials(std::move(data))
		{}

	};

}
