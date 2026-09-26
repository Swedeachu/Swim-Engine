#pragma once

#include "Engine/Systems/Renderer/Core/Textures/Texture2D.h"

#include <memory>
#include <utility>

namespace Engine
{

	// Transitional legacy renderer material payload. Geometry is deliberately not
	// part of material ownership: the mesh is selected by a render binding instead.
	struct MaterialData
	{
		std::shared_ptr<Texture2D> albedoMap;

		MaterialData() = default;

		explicit MaterialData(std::shared_ptr<Texture2D> albedoMap)
			: albedoMap(std::move(albedoMap))
		{}
	};

}
