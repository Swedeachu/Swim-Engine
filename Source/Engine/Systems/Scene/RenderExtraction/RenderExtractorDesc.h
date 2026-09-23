#pragma once

#include "Engine/Assets/AssetHandle.h"
#include "Engine/Assets/MeshAsset.h"
#include "Engine/Systems/Renderer/GpuScene/RenderAffine.h"
#include "Engine/Systems/Renderer/GpuScene/ResolvedRenderMesh.h"

#include <entt/entity/fwd.hpp>

#include <cstdint>
#include <functional>
#include <optional>

namespace Engine
{

	// Maps a mesh asset to a GPU-resident mesh, or empty while it is not resident
	// yet (AssetResidencyService::ResolveRenderMesh fits directly).
	using RenderMeshResolver =
		std::function<std::optional<Swim::Render::ResolvedRenderMesh>(Swim::Assets::AssetHandle<Swim::Assets::MeshAsset>)>;

	// Returns an entity's world transform. The runtime uses Transform components
	// (MakeTransformWorldSource); tests and other producers can supply their own.
	using RenderWorldTransformSource = std::function<Swim::Render::RenderAffine(const entt::registry&, entt::entity)>;

	// Optional per-entity id written to GpuInstanceRecord::ObjectId (picking,
	// debug views). Use a durable identity, never a recyclable registry handle.
	using RenderObjectIdSource = std::function<std::uint32_t(const entt::registry&, entt::entity)>;

	struct RenderExtractorDesc
	{
		// Identifies the scene in (scene, entity, sub-object) keys, for example
		// SceneId::GetValue(). One extractor serves one registry.
		std::uint64_t Scene = 0;
		RenderMeshResolver ResolveMesh;
		RenderWorldTransformSource WorldTransform;
		RenderObjectIdSource ObjectId; // Empty: ObjectId 0.
	};

} // namespace Engine
