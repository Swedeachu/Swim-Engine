#pragma once

#include <memory>
#include "Engine/Systems/Renderer/Meshes/Mesh.h"

namespace Engine
{

	struct Material
	{

		std::shared_ptr<Mesh> mesh;

		Material() = default;

		Material(const std::shared_ptr<Mesh>& meshPtr)
			: mesh(meshPtr)
		{}

	};

}