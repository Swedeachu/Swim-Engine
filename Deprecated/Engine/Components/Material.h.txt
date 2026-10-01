#pragma once

#include <memory>
#include <utility>
#include "Engine/Systems/Renderer/Core/Material/LegacyRenderBinding.h"

namespace Engine
{

	// Transitional scene render binding. Mesh and material are independent
	// resources; the component only pairs them for the legacy renderer.
	struct Material
	{
		std::shared_ptr<LegacyRenderBinding> binding;

		Material() = default;

		explicit Material(std::shared_ptr<LegacyRenderBinding> renderBinding)
			: binding(std::move(renderBinding))
		{}
	};

}
