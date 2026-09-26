#pragma once

#include "Engine/Assets/AssetHandle.h"
#include "Engine/Assets/MeshAsset.h"
#include "Engine/Assets/ModelAsset.h"
#include "Engine/Components/Tags.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Swim::Assets
{
	class AssetSystem;
}

namespace Engine
{
	class Scene;
	struct RenderServices;
} // namespace Engine

namespace Game
{
	// Finds a cooked model (a ".model" root loaded by the development asset bootstrap)
	// whose logical path contains every keyword (case-insensitive). Among matches, paths
	// containing a `prefer` keyword win (earlier keywords rank higher) and paths containing
	// an `avoid` keyword lose. Invalid when nothing matches.
	Swim::Assets::AssetHandle<Swim::Assets::ModelAsset> FindCookedModel(const Swim::Assets::AssetSystem& assets,
		const std::vector<std::string>& keywords, const std::vector<std::string>& prefer = {}, const std::vector<std::string>& avoid = {});

	struct ModelPlacement
	{
		// Where the bottom center of the model's bounds lands.
		glm::vec3 Position{ 0.0f };
		// Yaw about +Y, in degrees.
		float YawDegrees = 0.0f;
		// Uniform scale so the longest horizontal extent becomes this long (0 keeps the
		// model's own units).
		float TargetLength = 0.0f;
		std::vector<Engine::TagId> Tags;
	};

	// One drawable group of an imported model (one material).
	struct ImportedPart
	{
		std::string Name;
		Swim::Assets::AssetHandle<Swim::Assets::MeshAsset> Mesh;
		std::uint32_t MaterialSet = 0;
	};

	struct ImportedModel
	{
		std::vector<ImportedPart> Parts;
		// The entity transform every part uses (the placement).
		glm::vec3 Position{ 0.0f };
		glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		float Scale = 1.0f;
		std::vector<entt::entity> Entities;
		// World-space bounds of the placed model.
		glm::vec3 BoundsMin{ 0.0f };
		glm::vec3 BoundsMax{ 0.0f };
		std::uint32_t Triangles = 0;
		std::uint32_t Materials = 0;
		std::uint32_t Textures = 0;

		bool Valid() const { return !Parts.empty(); }
	};

	// Instantiates a cooked static model for the modern renderer. The GPU scene draws one
	// material per object, so the model's primitives are regrouped by material: every
	// (material) group becomes one mesh (node transforms baked in, vertices compacted,
	// tangents generated when the source has none) published through the MeshLibrary,
	// and one entity with a MeshRenderer. Materials come from the cooked material
	// instances (factors, alpha mode, double-sidedness and all five texture slots); their
	// textures are requested from residency (unsupported payloads fall back to white).
	// Meshes and materials are registered once per `name` and reused on scene reloads.
	// Requires a renderer; returns an empty result without one or on unusable data.
	ImportedModel SpawnCookedModel(Engine::Scene& scene, Engine::RenderServices& render, Swim::Assets::AssetSystem& assets,
		Swim::Assets::AssetHandle<Swim::Assets::ModelAsset> model, std::string_view name, const ModelPlacement& placement);

	// Creates the entities of an already imported model again (after a scene reset): no
	// CPU geometry work, the meshes and materials are already registered.
	std::vector<entt::entity> RespawnModel(Engine::Scene& scene, const ImportedModel& model, const std::vector<Engine::TagId>& tags);
} // namespace Game
