#pragma once

#include <memory>
#include "Engine/Systems/Renderer/Meshes/Mesh.h"
#include "Engine/Systems/Renderer/Textures/Texture2D.h"
#include "Engine/Systems/Renderer/PBR/MaterialDescriptor.h"

namespace Engine
{

	struct Material
	{

		std::shared_ptr<Mesh> mesh;
		std::shared_ptr<Texture2D> albedoMap; // this is the raw texture
		// std::shared_ptr<Texture2D> normalMap; // for light
		// std::shared_ptr<Texture2D> roughnessMap; // height map technically

		std::shared_ptr<MaterialDescriptor> materialDescriptor;

		Material() = default;

		Material(const std::shared_ptr<Mesh>& meshPtr)
			: mesh(meshPtr)
		{}

	};

}