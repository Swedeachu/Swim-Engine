#pragma once

#include "Engine/Systems/Renderer/Core/Meshes/Mesh.h"
#include "Engine/Systems/Renderer/Core/Textures/Texture2D.h"

namespace Engine
{

	struct MaterialData
	{

		std::shared_ptr<Mesh> mesh;
		std::shared_ptr<Texture2D> albedoMap; // this is the raw texture
		// std::shared_ptr<Texture2D> normalMap; // for light
		// std::shared_ptr<Texture2D> roughnessMap; // height map technically

		MaterialData() = default;

		MaterialData(const std::shared_ptr<Mesh>& meshPtr, std::shared_ptr<Texture2D>& albedoMap)
			: mesh(meshPtr), albedoMap(albedoMap)
		{}

	};

}
