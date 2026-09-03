#pragma once

#include "Engine/Systems/Renderer/Core/Material/MaterialData.h"
#include "Engine/Systems/Renderer/Core/Meshes/Mesh.h"
#include "Engine/Systems/Renderer/Core/Meshes/MeshBufferData.h"

#include <memory>
#include <utility>

namespace Engine
{

	// Compatibility-only draw binding used while the old renderer is migrated to
	// AssetHandle<MeshAsset> + AssetHandle<MaterialInstanceAsset>. This is not an
	// owning material model; it pairs independent legacy mesh/material residencies
	// only at the draw boundary.
	struct LegacyRenderBinding
	{
		std::shared_ptr<Mesh> mesh;
		std::shared_ptr<MeshBufferData> meshBufferData;
		std::shared_ptr<MaterialData> material;

		LegacyRenderBinding() = default;

		LegacyRenderBinding(std::shared_ptr<Mesh> mesh, std::shared_ptr<MeshBufferData> meshBufferData, std::shared_ptr<MaterialData> material)
			: mesh(std::move(mesh)), meshBufferData(std::move(meshBufferData)), material(std::move(material))
		{}

		std::shared_ptr<Texture2D> GetAlbedoMap() const
		{
			return material ? material->albedoMap : nullptr;
		}
	};

}
