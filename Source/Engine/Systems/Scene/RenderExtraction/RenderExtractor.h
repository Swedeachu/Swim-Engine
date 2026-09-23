#pragma once

#include "Engine/Components/MeshRenderer.h"
#include "Engine/Systems/Renderer/GpuScene/GpuScene.h"
#include "Engine/Systems/Scene/RenderExtraction/RenderExtractionStats.h"
#include "Engine/Systems/Scene/RenderExtraction/RenderExtractorDesc.h"
#include "Engine/Systems/Scene/TransformSystem.h"

#include <entt/entity/registry.hpp>

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Engine
{

	// EnTT -> GPU Scene extraction (critical-path item 47). The GPU Scene is the
	// persistent render database; this is one producer of its updates. Every
	// MeshRenderer part becomes one RenderObject keyed by (scene, entity,
	// sub-object), so the renderer keeps stable rows while EnTT storage moves.
	//
	// Nothing is rebuilt per frame. Extract applies only:
	//   - MeshRenderer constructions/updates/removals observed through registry
	//     signals (create / reconcile / destroy render objects);
	//   - entities in TransformSystem's dirty list for this frame (children of a
	//     moved parent are queued by Transform itself) -> SetTransform;
	//   - parts whose mesh was not GPU-resident yet -> SetMesh once it resolves.
	// Static objects cost nothing after their first extraction.
	//
	// Call Extract once per frame after scene updates and before the scene's
	// BeginFrameTransformTracking, then GpuScene::Import. Owner thread only.
	// Destroy the extractor before the registry; call ReleaseAll first when the
	// scene unloads so its objects retire through the GPU Scene's deferred path.
	class RenderExtractor
	{

	  public:
		RenderExtractor(entt::registry& registry, TransformSystem& transforms, Swim::Render::GpuScene& scene, RenderExtractorDesc desc);
		~RenderExtractor();
		RenderExtractor(const RenderExtractor&) = delete;
		RenderExtractor& operator=(const RenderExtractor&) = delete;

		// lastUse is the latest submitted GPU work that may read objects destroyed
		// by this call (removed components/entities, dropped parts).
		RenderExtractionStats Extract(Swim::Rhi::TimelinePoint lastUse = {});

		// Scene unload: destroys every object this extractor created and forgets
		// queued changes. Existing MeshRenderers are extracted again by a later
		// Extract only if they change; call Rescan to recreate them all.
		void ReleaseAll(Swim::Rhi::TimelinePoint lastUse = {});

		// Queues every MeshRenderer in the registry, for example after ReleaseAll.
		void Rescan();

		// Re-resolves every part's mesh at the next Extract (for example after
		// residency released or replaced meshes).
		void RefreshMeshes();

		std::uint64_t GetScene() const { return scene; }

		// Invalid when the entity/sub-object has no live render object.
		Swim::Render::RenderObjectHandle Find(entt::entity entity, std::uint32_t subObject = 0) const;

		std::uint32_t GetTrackedEntities() const { return static_cast<std::uint32_t>(entities.size()); }

		std::uint32_t GetLiveObjects() const { return liveObjects; }

	  private:
		struct Part
		{
			Swim::Render::RenderObjectHandle Object;
			MeshRendererPart Source;
			bool MeshResolved = false;
		};

		struct EntityState
		{
			std::vector<Part> Parts;
		};

		void OnChanged(entt::registry& registry, entt::entity entity);
		void OnDestroyed(entt::registry& registry, entt::entity entity);

		void DestroyEntity(entt::entity entity, Swim::Rhi::TimelinePoint lastUse, RenderExtractionStats& stats);
		void Reconcile(entt::entity entity, const MeshRenderer& renderer, Swim::Rhi::TimelinePoint lastUse, RenderExtractionStats& stats);
		bool CreatePart(entt::entity entity, const MeshRenderer& renderer, const Swim::Render::RenderAffine& world, Part& part,
			RenderExtractionStats& stats);
		bool ResolvePart(Part& part);
		Swim::Render::RenderAffine WorldOf(entt::entity entity) const;

		entt::registry& registry;
		TransformSystem& transforms;
		Swim::Render::GpuScene& gpuScene;
		std::uint64_t scene = 0;
		RenderMeshResolver resolveMesh;
		RenderWorldTransformSource worldTransform;
		RenderObjectIdSource objectId;

		std::unordered_map<entt::entity, EntityState> entities;
		std::unordered_set<entt::entity> changed;
		std::vector<entt::entity> changedOrder; // Signal order, so row assignment is deterministic.
		std::vector<entt::entity> destroyed;
		std::unordered_set<entt::entity> pendingMeshes; // Entities with unresolved or uncreated parts.
		std::uint32_t liveObjects = 0;
		bool refreshMeshes = false;
	};

} // namespace Engine
