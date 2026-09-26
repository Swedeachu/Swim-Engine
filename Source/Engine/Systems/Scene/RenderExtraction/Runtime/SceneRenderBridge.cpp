#include "Engine/Systems/Scene/RenderExtraction/Runtime/SceneRenderBridge.h"

#include "Engine/Components/Light.h"
#include "Engine/Components/ParticleEmitter.h"
#include "Engine/Components/SkinnedMeshRenderer.h"
#include "Engine/Components/Transform.h"
#include "Engine/Systems/Renderer/GpuScene/GpuScene.h"
#include "Engine/Systems/Renderer/Lights/GpuLightBuffer.h"
#include "Engine/Systems/Renderer/Lights/LightMath.h"
#include "Engine/Systems/Renderer/Particles/ParticleSystem.h"
#include "Engine/Systems/Renderer/Residency/AssetResidencyService.h"
#include "Engine/Systems/Renderer/Runtime/FrameRenderer.h"
#include "Engine/Systems/Renderer/Runtime/MeshLibrary.h"
#include "Engine/Systems/Renderer/Skinning/SkinningSystem.h"
#include "Engine/Systems/Scene/RenderExtraction/RenderExtractor.h"
#include "Engine/Systems/Scene/RenderExtraction/Runtime/TransformWorldSource.h"
#include "Engine/Systems/Scene/Scene.h"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

namespace Engine
{
	namespace
	{
		std::array<float, 12> WorldRows(const entt::registry& registry, const Transform& transform)
		{
			return Swim::Render::RenderAffine::FromColumnMajor(glm::value_ptr(transform.GetWorldMatrix(registry))).Rows;
		}

		bool SameLight(const Swim::Render::LightDesc& a, const Swim::Render::LightDesc& b)
		{
			return a.Type == b.Type && a.Position == b.Position && a.Direction == b.Direction && a.Color == b.Color &&
				a.Intensity == b.Intensity && a.Range == b.Range && a.InnerConeAngle == b.InnerConeAngle &&
				a.OuterConeAngle == b.OuterConeAngle && a.ShadowIndex == b.ShadowIndex && a.Flags == b.Flags;
		}

		float Finite(float value, float fallback)
		{
			return std::isfinite(value) ? value : fallback;
		}
	} // namespace

	SceneRenderBridge::SceneRenderBridge(FrameRenderer& rendererValue) : renderer(rendererValue)
	{
	}

	SceneRenderBridge::~SceneRenderBridge()
	{
		Detach();
		for (auto& [name, mesh] : skinnedMeshes)
		{
			(void)name;
			renderer.GetSkinning().DestroySkinnedMesh(mesh.Handle, renderer.GetLastCompletion());
		}
	}

	void SceneRenderBridge::Attach(Scene* value, std::uint64_t id)
	{
		if (value == scene && id == sceneId)
		{
			return;
		}
		Detach();
		scene = value;
		sceneId = id;
		if (!scene)
		{
			return;
		}
		RenderExtractorDesc desc;
		desc.Scene = sceneId;
		desc.ResolveMesh = [&residency = renderer.GetResidency()](Swim::Assets::AssetHandle<Swim::Assets::MeshAsset> mesh)
		{
			return residency.ResolveRenderMesh(mesh);
		};
		desc.WorldTransform = MakeTransformWorldSource();
		Scene* owner = scene;
		desc.ObjectId = [owner](const entt::registry&, entt::entity entity)
		{
			// Durable ids, below 2^24 so the float object-id target holds them exactly.
			return static_cast<std::uint32_t>(owner->GetSerializedEntityId(entity).Value & 0xffffffu);
		};
		extractor =
			std::make_unique<RenderExtractor>(scene->GetRegistry(), scene->GetTransformSystem(), renderer.GetScene(), std::move(desc));
		extractor->Rescan();
	}

	void SceneRenderBridge::Detach()
	{
		ReleaseAll();
		if (extractor)
		{
			extractor->ReleaseAll(renderer.GetLastCompletion());
			extractor.reset();
		}
		scene = nullptr;
		sceneId = 0;
	}

	void SceneRenderBridge::ReleaseAll()
	{
		for (auto& [entity, state] : lights)
		{
			(void)entity;
			ReleaseLight(state);
		}
		lights.clear();
		for (auto& [entity, state] : emitters)
		{
			(void)entity;
			renderer.GetParticles().Release(state.Handle, renderer.GetLastCompletion());
		}
		emitters.clear();
		for (auto& [entity, state] : skins)
		{
			(void)entity;
			ReleaseSkin(state);
		}
		skins.clear();
		casters.clear();
	}

	void SceneRenderBridge::ReleaseLight(LightState& state)
	{
		renderer.GetLights().Release(state.Handle);
		if (state.Slot != UINT32_MAX && state.Slot < shadowSlots.size())
		{
			shadowSlots[state.Slot] = false;
		}
		state.Slot = UINT32_MAX;
	}

	void SceneRenderBridge::ReleaseSkin(SkinState& state)
	{
		const auto lastUse = renderer.GetLastCompletion();
		if (state.Object)
		{
			renderer.GetScene().Destroy(state.Object, lastUse);
		}
		renderer.GetMeshes().UntrackGpuMesh(state.Output);
		if (state.Instance)
		{
			renderer.GetSkinning().DestroyInstance(state.Instance, lastUse);
		}
		state = {};
	}

	std::uint32_t SceneRenderBridge::AllocateShadowSlot()
	{
		const std::uint32_t maxSlots = renderer.GetSettings().Shadow.MaxSlots;
		if (shadowSlots.size() != maxSlots)
		{
			shadowSlots.resize(maxSlots, false);
		}
		for (std::uint32_t slot = 0; slot < maxSlots; ++slot)
		{
			if (!shadowSlots[slot])
			{
				shadowSlots[slot] = true;
				return slot;
			}
		}
		return UINT32_MAX;
	}

	void SceneRenderBridge::Update()
	{
		casters.clear();
		if (!scene)
		{
			return;
		}
		auto& registry = scene->GetRegistry();
		if (extractor)
		{
			extraction = extractor->Extract(renderer.GetLastCompletion());
		}
		UpdateLights(registry);
		UpdateEmitters(registry);
		UpdateSkins(registry);
	}

	void SceneRenderBridge::UpdateLights(entt::registry& registry)
	{
		using namespace Swim::Render;
		auto& buffer = renderer.GetLights();
		// Forget lights whose entity or component went away.
		for (auto it = lights.begin(); it != lights.end();)
		{
			const bool alive =
				registry.valid(it->first) && registry.all_of<Light, Transform>(it->first) && registry.get<Light>(it->first).Enabled;
			if (alive)
			{
				++it;
				continue;
			}
			ReleaseLight(it->second);
			it = lights.erase(it);
		}

		auto view = registry.view<Light, Transform>();
		for (const entt::entity entity : view)
		{
			const auto& light = view.get<Light>(entity);
			if (!light.Enabled)
			{
				continue;
			}
			const auto& transform = view.get<Transform>(entity);
			const glm::mat4& world = transform.GetWorldMatrix(registry);
			glm::vec3 forward = -glm::vec3(world[2]);
			forward = glm::length(forward) > 1e-6f ? glm::normalize(forward) : glm::vec3(0, -1, 0);

			LightDesc desc;
			desc.Type = light.Kind == LightKind::Directional ? LightType::Directional
															 : (light.Kind == LightKind::Spot ? LightType::Spot : LightType::Point);
			desc.Position = { world[3].x, world[3].y, world[3].z };
			desc.Direction = { forward.x, forward.y, forward.z };
			desc.Color = { std::max(Finite(light.Color.r, 1), 0.0f), std::max(Finite(light.Color.g, 1), 0.0f),
				std::max(Finite(light.Color.b, 1), 0.0f) };
			desc.Intensity = std::max(Finite(light.Intensity, 0), 0.0f);
			desc.Range = std::max(Finite(light.Range, 10), 0.01f);
			desc.OuterConeAngle = std::clamp(Finite(light.OuterCone, 0.6f), 0.01f, 1.5707f);
			desc.InnerConeAngle = std::clamp(Finite(light.InnerCone, 0.3f), 0.0f, desc.OuterConeAngle * 0.999f);

			auto [it, inserted] = lights.try_emplace(entity);
			LightState& state = it->second;
			const bool wantsShadow = light.CastShadows && renderer.GetSettings().Shadows;
			if (wantsShadow && state.Slot == UINT32_MAX)
			{
				state.Slot = AllocateShadowSlot();
			}
			else if (!wantsShadow && state.Slot != UINT32_MAX)
			{
				shadowSlots[state.Slot] = false;
				state.Slot = UINT32_MAX;
			}
			if (state.Slot != UINT32_MAX)
			{
				desc.ShadowIndex = state.Slot;
				desc.Flags = LightFlags::CastsShadows;
			}

			if (inserted || !buffer.IsValid(state.Handle))
			{
				const auto handle = buffer.TryCreate(desc);
				if (!handle)
				{
					if (state.Slot != UINT32_MAX)
					{
						shadowSlots[state.Slot] = false;
					}
					lights.erase(it);
					continue; // The light buffer is full.
				}
				state.Handle = *handle;
				state.Desc = desc;
			}
			else if (!SameLight(state.Desc, desc))
			{
				buffer.Update(state.Handle, desc);
				state.Desc = desc;
			}
			if (state.Slot != UINT32_MAX)
			{
				ShadowCasterDesc caster;
				caster.Slot = state.Slot;
				caster.Light = Lights::EncodeLight(desc);
				caster.Priority = light.ShadowPriority + (desc.Type == LightType::Directional ? 1000.0f : 0.0f);
				casters.push_back(caster);
			}
		}
		// Deterministic planner input (slot order).
		std::sort(casters.begin(), casters.end(),
			[](const ShadowCasterDesc& a, const ShadowCasterDesc& b)
			{
				return a.Slot < b.Slot;
			});
	}

	void SceneRenderBridge::UpdateEmitters(entt::registry& registry)
	{
		auto& particles = renderer.GetParticles();
		const auto lastUse = renderer.GetLastCompletion();
		for (auto it = emitters.begin(); it != emitters.end();)
		{
			if (registry.valid(it->first) && registry.all_of<ParticleEmitter, Transform>(it->first))
			{
				++it;
				continue;
			}
			particles.Release(it->second.Handle, lastUse);
			it = emitters.erase(it);
		}
		auto view = registry.view<ParticleEmitter, Transform>();
		for (const entt::entity entity : view)
		{
			const auto& emitter = view.get<ParticleEmitter>(entity);
			const auto rows = WorldRows(registry, view.get<Transform>(entity));
			auto found = emitters.find(entity);
			if (found != emitters.end() && found->second.Revision != emitter.Revision)
			{
				particles.Release(found->second.Handle, lastUse);
				emitters.erase(found);
				found = emitters.end();
			}
			if (found == emitters.end())
			{
				try
				{
					const auto handle = particles.TryCreateEmitter(emitter.Desc, rows);
					if (!handle)
					{
						continue; // No room this frame.
					}
					found = emitters.emplace(entity, EmitterState{ *handle, emitter.Revision, true }).first;
				}
				catch (const std::exception& error)
				{
					std::cerr << "[Render] Invalid particle emitter on entity " << static_cast<std::uint32_t>(entity) << ": "
							  << error.what() << '\n';
					continue;
				}
			}
			particles.SetTransform(found->second.Handle, rows);
			if (found->second.Emitting != emitter.Emitting)
			{
				particles.SetEmitting(found->second.Handle, emitter.Emitting);
				found->second.Emitting = emitter.Emitting;
			}
		}
	}

	bool SceneRenderBridge::RegisterSkinnedMesh(const std::string& name, const ProceduralMeshes::SkinnedMeshData& mesh)
	{
		if (skinnedMeshes.contains(name))
		{
			return true;
		}
		const std::array<Swim::Render::GeometrySubmesh, 1> submeshes{
			{ { 0, static_cast<std::uint32_t>(mesh.Mesh.Indices.size()), 0, 0 } }
		};
		const std::array<Swim::Render::GeometryLodRange, 1> lods{ { { 0, 1, 0.0f } } };
		Swim::Render::SkinnedMeshDesc desc;
		desc.Vertices = mesh.Mesh.Vertices;
		desc.Influences = mesh.Influences;
		desc.JointCount = mesh.JointCount;
		desc.IndexFormat = Swim::Rhi::IndexType::Uint32;
		desc.Indices = std::as_bytes(std::span(mesh.Mesh.Indices));
		desc.Submeshes = submeshes;
		desc.Lods = lods;
		desc.DebugName = name;
		try
		{
			skinnedMeshes[name] = { renderer.GetSkinning().CreateSkinnedMesh(desc), mesh.JointCount };
		}
		catch (const std::exception& error)
		{
			std::cerr << "[Render] Cannot register skinned mesh '" << name << "': " << error.what() << '\n';
			return false;
		}
		return true;
	}

	std::uint32_t SceneRenderBridge::GetJointCount(const std::string& name) const
	{
		const auto found = skinnedMeshes.find(name);
		return found == skinnedMeshes.end() ? 0u : found->second.JointCount;
	}

	void SceneRenderBridge::UpdateSkins(entt::registry& registry)
	{
		using namespace Swim::Render;
		auto& skinning = renderer.GetSkinning();
		auto& gpuScene = renderer.GetScene();
		for (auto it = skins.begin(); it != skins.end();)
		{
			const bool alive = registry.valid(it->first) && registry.all_of<SkinnedMeshRenderer, Transform>(it->first) &&
				registry.get<SkinnedMeshRenderer>(it->first).Mesh == it->second.Mesh;
			if (alive)
			{
				++it;
				continue;
			}
			ReleaseSkin(it->second);
			it = skins.erase(it);
		}
		auto view = registry.view<SkinnedMeshRenderer, Transform>();
		for (const entt::entity entity : view)
		{
			const auto& component = view.get<SkinnedMeshRenderer>(entity);
			const auto mesh = skinnedMeshes.find(component.Mesh);
			if (mesh == skinnedMeshes.end())
			{
				continue;
			}
			const RenderAffine world{ WorldRows(registry, view.get<Transform>(entity)) };
			auto found = skins.find(entity);
			if (found == skins.end())
			{
				SkinState state;
				state.Mesh = component.Mesh;
				try
				{
					state.Instance = skinning.CreateInstance(mesh->second.Handle);
				}
				catch (const std::exception& error)
				{
					std::cerr << "[Render] Cannot create a skinned instance: " << error.what() << '\n';
					continue;
				}
				state.Output = skinning.GetOutputMesh(state.Instance);
				renderer.GetMeshes().TrackGpuMesh(state.Output);
				RenderObjectDesc desc;
				desc.Transform = world;
				desc.Mesh = state.Output;
				desc.LocalBounds = skinning.ComputeBounds(state.Instance);
				desc.MaterialSet = component.MaterialSet;
				desc.ObjectId = static_cast<std::uint32_t>(scene->GetSerializedEntityId(entity).Value & 0xffffffu);
				desc.Flags = component.Flags;
				desc.PreviousVertexOffset = skinning.GetPreviousVertexOffset(state.Instance);
				const auto object = gpuScene.TryCreate(desc);
				if (!object)
				{
					skinning.DestroyInstance(state.Instance, renderer.GetLastCompletion());
					renderer.GetMeshes().UntrackGpuMesh(state.Output);
					continue;
				}
				state.Object = *object;
				state.MaterialSet = component.MaterialSet;
				state.Flags = static_cast<std::uint32_t>(component.Flags);
				found = skins.emplace(entity, std::move(state)).first;
			}
			auto& state = found->second;
			gpuScene.SetTransform(state.Object, world);
			if (state.MaterialSet != component.MaterialSet)
			{
				gpuScene.SetMaterialSet(state.Object, component.MaterialSet);
				state.MaterialSet = component.MaterialSet;
			}
			if (state.Flags != static_cast<std::uint32_t>(component.Flags))
			{
				gpuScene.SetFlags(state.Object, component.Flags);
				state.Flags = static_cast<std::uint32_t>(component.Flags);
			}
			if (component.PoseRevision != state.PoseRevision && component.Palette.size() == mesh->second.JointCount)
			{
				if (state.Previous.size() != component.Palette.size())
				{
					state.Previous = component.Palette;
				}
				static const std::vector<float> noWeights;
				SkinPose pose;
				pose.Palette = component.Palette;
				pose.PreviousPalette = state.Previous;
				pose.MorphWeights = noWeights;
				pose.PreviousMorphWeights = noWeights;
				try
				{
					skinning.SetPose(state.Instance, pose);
					gpuScene.SetBounds(state.Object, skinning.ComputeBounds(state.Instance));
				}
				catch (const std::exception& error)
				{
					std::cerr << "[Render] Invalid skin pose: " << error.what() << '\n';
				}
				state.Previous = component.Palette;
				state.PoseRevision = component.PoseRevision;
			}
		}
	}
} // namespace Engine
