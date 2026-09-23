#include "Engine/Systems/Scene/RenderExtraction/RenderExtractor.h"

#include <algorithm>

namespace Engine
{

	namespace
	{
		using Swim::Render::RenderObjectHandle;
	}

	RenderExtractor::RenderExtractor(
		entt::registry& registry, TransformSystem& transforms, Swim::Render::GpuScene& scene, RenderExtractorDesc desc)
		: registry(registry), transforms(transforms), gpuScene(scene), scene(desc.Scene), resolveMesh(std::move(desc.ResolveMesh)),
		  worldTransform(std::move(desc.WorldTransform)), objectId(std::move(desc.ObjectId))
	{
		registry.on_construct<MeshRenderer>().connect<&RenderExtractor::OnChanged>(*this);
		registry.on_update<MeshRenderer>().connect<&RenderExtractor::OnChanged>(*this);
		registry.on_destroy<MeshRenderer>().connect<&RenderExtractor::OnDestroyed>(*this);
		Rescan();
	}

	RenderExtractor::~RenderExtractor()
	{
		registry.on_construct<MeshRenderer>().disconnect(this);
		registry.on_update<MeshRenderer>().disconnect(this);
		registry.on_destroy<MeshRenderer>().disconnect(this);
	}

	void RenderExtractor::OnChanged(entt::registry&, entt::entity entity)
	{
		if (changed.insert(entity).second)
		{
			changedOrder.push_back(entity);
		}
	}

	void RenderExtractor::OnDestroyed(entt::registry&, entt::entity entity)
	{
		changed.erase(entity);
		destroyed.push_back(entity);
	}

	void RenderExtractor::Rescan()
	{
		for (const auto entity : registry.view<MeshRenderer>())
		{
			OnChanged(registry, entity);
		}
	}

	void RenderExtractor::RefreshMeshes()
	{
		refreshMeshes = true;
	}

	Swim::Render::RenderAffine RenderExtractor::WorldOf(entt::entity entity) const
	{
		return worldTransform ? worldTransform(registry, entity) : Swim::Render::RenderAffine{};
	}

	RenderObjectHandle RenderExtractor::Find(entt::entity entity, std::uint32_t subObject) const
	{
		const auto found = entities.find(entity);
		if (found == entities.end() || subObject >= found->second.Parts.size())
		{
			return {};
		}
		const auto object = found->second.Parts[subObject].Object;
		return gpuScene.IsValid(object) ? object : RenderObjectHandle{};
	}

	bool RenderExtractor::ResolvePart(Part& part)
	{
		if (!gpuScene.IsValid(part.Object))
		{
			return false;
		}
		std::optional<Swim::Render::ResolvedRenderMesh> mesh;
		if (resolveMesh && part.Source.Mesh)
		{
			mesh = resolveMesh(part.Source.Mesh);
		}
		if (mesh)
		{
			gpuScene.SetMesh(part.Object, mesh->Mesh, mesh->LocalBounds);
		}
		else
		{
			gpuScene.SetMesh(part.Object, {}, Swim::Render::RenderBounds::Infinite());
		}
		part.MeshResolved = mesh.has_value();
		return part.MeshResolved;
	}

	bool RenderExtractor::CreatePart(entt::entity entity, const MeshRenderer& renderer, const Swim::Render::RenderAffine& world, Part& part,
		RenderExtractionStats& stats)
	{
		Swim::Render::RenderObjectDesc desc;
		desc.Transform = world;
		desc.MaterialSet = part.Source.MaterialSet;
		desc.ObjectId = objectId ? objectId(registry, entity) : 0;
		desc.Flags = renderer.Flags;
		desc.SkinIndex = renderer.SkinIndex;
		desc.LodBias = renderer.LodBias;
		const auto object = gpuScene.TryCreate(desc);
		if (!object)
		{
			++stats.CapacityFailures;
			return false;
		}
		part.Object = *object;
		++liveObjects;
		++stats.ObjectsCreated;
		if (ResolvePart(part))
		{
			++stats.MeshesResolved;
		}
		return true;
	}

	void RenderExtractor::DestroyEntity(entt::entity entity, Swim::Rhi::TimelinePoint lastUse, RenderExtractionStats& stats)
	{
		const auto found = entities.find(entity);
		if (found == entities.end())
		{
			return;
		}
		for (const auto& part : found->second.Parts)
		{
			if (gpuScene.Destroy(part.Object, lastUse))
			{
				--liveObjects;
				++stats.ObjectsDestroyed;
			}
		}
		entities.erase(found);
		pendingMeshes.erase(entity);
		++stats.EntitiesDestroyed;
	}

	void RenderExtractor::Reconcile(
		entt::entity entity, const MeshRenderer& renderer, Swim::Rhi::TimelinePoint lastUse, RenderExtractionStats& stats)
	{
		auto& state = entities[entity];
		const auto world = WorldOf(entity);
		// Parts beyond the new count are dropped.
		while (state.Parts.size() > renderer.Parts.size())
		{
			if (gpuScene.Destroy(state.Parts.back().Object, lastUse))
			{
				--liveObjects;
				++stats.ObjectsDestroyed;
			}
			state.Parts.pop_back();
		}
		bool unresolved = false;
		for (std::size_t index = 0; index < renderer.Parts.size(); ++index)
		{
			if (index == state.Parts.size())
			{
				state.Parts.push_back({ {}, renderer.Parts[index], false });
			}
			auto& part = state.Parts[index];
			if (!gpuScene.IsValid(part.Object))
			{
				part.Source = renderer.Parts[index];
				if (!CreatePart(entity, renderer, world, part, stats))
				{
					unresolved = true;
					continue;
				}
				++stats.TransformsWritten;
				unresolved = unresolved || !part.MeshResolved;
				continue;
			}
			const bool meshChanged = part.Source.Mesh != renderer.Parts[index].Mesh;
			part.Source = renderer.Parts[index];
			gpuScene.SetMaterialSet(part.Object, part.Source.MaterialSet);
			gpuScene.SetFlags(part.Object, renderer.Flags);
			gpuScene.SetSkin(part.Object, renderer.SkinIndex);
			gpuScene.SetLodBias(part.Object, renderer.LodBias);
			gpuScene.SetTransform(part.Object, world);
			++stats.TransformsWritten;
			if (meshChanged || !part.MeshResolved)
			{
				if (ResolvePart(part))
				{
					++stats.MeshesResolved;
				}
			}
			unresolved = unresolved || !part.MeshResolved;
			++stats.ObjectsUpdated;
		}
		if (unresolved)
		{
			pendingMeshes.insert(entity);
		}
		else
		{
			pendingMeshes.erase(entity);
		}
	}

	RenderExtractionStats RenderExtractor::Extract(Swim::Rhi::TimelinePoint lastUse)
	{
		RenderExtractionStats stats;

		// Removals first, so a component removed and re-added this frame is rebuilt.
		auto removed = std::move(destroyed);
		destroyed.clear();
		for (const auto entity : removed)
		{
			DestroyEntity(entity, lastUse, stats);
		}

		// Signal order (not hash order) keeps row assignment deterministic. An entry
		// whose component was removed again, or a repeat, is no longer in `changed`.
		auto order = std::move(changedOrder);
		changedOrder.clear();
		std::unordered_set<entt::entity> updates;
		for (const auto entity : order)
		{
			if (!changed.erase(entity))
			{
				continue;
			}
			updates.insert(entity);
			const auto* renderer = registry.valid(entity) ? registry.try_get<MeshRenderer>(entity) : nullptr;
			if (!renderer)
			{
				DestroyEntity(entity, lastUse, stats);
				continue;
			}
			Reconcile(entity, *renderer, lastUse, stats);
			++stats.EntitiesChanged;
		}

		// Transform-only changes: the scene's per-frame dirty list, not a full walk.
		for (const auto entity : transforms.GetDirtyEntities())
		{
			if (updates.contains(entity))
			{
				continue; // Reconcile already wrote this frame's transform.
			}
			const auto found = entities.find(entity);
			if (found == entities.end())
			{
				continue;
			}
			const auto world = WorldOf(entity);
			for (const auto& part : found->second.Parts)
			{
				if (gpuScene.SetTransform(part.Object, world))
				{
					++stats.TransformsWritten;
				}
			}
		}

		if (refreshMeshes)
		{
			refreshMeshes = false;
			for (auto& [entity, state] : entities)
			{
				for (auto& part : state.Parts)
				{
					part.MeshResolved = false;
				}
				pendingMeshes.insert(entity);
			}
		}

		// Parts waiting for a resident mesh or a free GPU Scene row.
		for (auto it = pendingMeshes.begin(); it != pendingMeshes.end();)
		{
			const auto entity = *it;
			auto found = entities.find(entity);
			const auto* renderer = registry.valid(entity) ? registry.try_get<MeshRenderer>(entity) : nullptr;
			if (found == entities.end() || !renderer)
			{
				it = pendingMeshes.erase(it);
				continue;
			}
			bool unresolved = false;
			for (auto& part : found->second.Parts)
			{
				if (!gpuScene.IsValid(part.Object))
				{
					if (!CreatePart(entity, *renderer, WorldOf(entity), part, stats))
					{
						unresolved = true;
						continue;
					}
					++stats.TransformsWritten;
				}
				else if (!part.MeshResolved && ResolvePart(part))
				{
					++stats.MeshesResolved;
				}
				if (!part.MeshResolved)
				{
					unresolved = true;
					++stats.PendingMeshes;
				}
			}
			it = unresolved ? std::next(it) : pendingMeshes.erase(it);
		}

		stats.TrackedEntities = static_cast<std::uint32_t>(entities.size());
		stats.LiveObjects = liveObjects;
		return stats;
	}

	void RenderExtractor::ReleaseAll(Swim::Rhi::TimelinePoint lastUse)
	{
		for (const auto& [entity, state] : entities)
		{
			for (const auto& part : state.Parts)
			{
				gpuScene.Destroy(part.Object, lastUse);
			}
		}
		entities.clear();
		liveObjects = 0;
		changed.clear();
		changedOrder.clear();
		destroyed.clear();
		pendingMeshes.clear();
		refreshMeshes = false;
	}

} // namespace Engine
