#pragma once

#include "Engine/Systems/Renderer/Lights/LightDesc.h"
#include "Engine/Systems/Renderer/Particles/ParticleGraphResources.h"
#include "Engine/Systems/Renderer/Particles/ParticleSettings.h"
#include "Engine/Systems/Renderer/Skinning/SkinningGraphResources.h"
#include "Engine/Systems/Renderer/Resources/GpuHandle.h"
#include "Engine/Systems/Renderer/Runtime/ProceduralMeshes.h"
#include "Engine/Systems/Renderer/Shadows/ShadowPlanner.h"
#include "Engine/Systems/Scene/RenderExtraction/RenderExtractionStats.h"

#include <entt/entt.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace Engine
{
	class FrameRenderer;
	class RenderExtractor;
	class Scene;

	// Scene -> renderer bridge (Phase 23): everything the active scene contributes to a
	// frame, applied incrementally once per frame after the scene updated.
	//
	//   MeshRenderer         -> GpuScene objects (RenderExtractor: created/changed/destroyed
	//                           components, dirty transforms, meshes that became resident)
	//   Light                -> GpuLightBuffer rows (+ shadow slots and this frame's casters)
	//   ParticleEmitter      -> ParticleSystem emitters following their entity
	//   SkinnedMeshRenderer  -> SkinningSystem instances + GpuScene objects on their output
	//                           meshes, posed from the component's palette
	//
	// Attach switches scenes (releasing everything the previous one created); a scene
	// reload destroys its entities, which releases their GPU objects through the same
	// paths. Owner thread only; the renderer must outlive the bridge.
	class SceneRenderBridge
	{
	  public:
		explicit SceneRenderBridge(FrameRenderer& renderer);
		~SceneRenderBridge();
		SceneRenderBridge(const SceneRenderBridge&) = delete;
		SceneRenderBridge& operator=(const SceneRenderBridge&) = delete;

		void Attach(Scene* scene, std::uint64_t sceneId);
		void Detach();

		Scene* GetAttached() const { return scene; }

		// Call once per frame after the scene's updates, before rendering.
		void Update();

		std::span<const Swim::Render::ShadowCasterDesc> GetShadowCasters() const { return casters; }

		const RenderExtractionStats& GetExtractionStats() const { return extraction; }

		std::uint32_t GetLightCount() const { return static_cast<std::uint32_t>(lights.size()); }

		std::uint32_t GetEmitterCount() const { return static_cast<std::uint32_t>(emitters.size()); }

		std::uint32_t GetSkinnedCount() const { return static_cast<std::uint32_t>(skins.size()); }

		// Registers a skinned mesh (bind pose + influences) under a name for
		// SkinnedMeshRenderer::Mesh. Re-registering a name keeps the first mesh.
		bool RegisterSkinnedMesh(const std::string& name, const ProceduralMeshes::SkinnedMeshData& mesh);

		bool HasSkinnedMesh(const std::string& name) const { return skinnedMeshes.contains(name); }

		std::uint32_t GetJointCount(const std::string& name) const;

	  private:
		struct LightState
		{
			Swim::Render::GpuLightHandle Handle;
			Swim::Render::LightDesc Desc;
			std::uint32_t Slot = UINT32_MAX;
		};

		struct EmitterState
		{
			Swim::Render::ParticleEmitterHandle Handle;
			std::uint64_t Revision = 0;
			bool Emitting = true;
		};

		struct SkinnedMeshState
		{
			Swim::Render::SkinnedMeshHandle Handle;
			std::uint32_t JointCount = 0;
		};

		struct SkinState
		{
			Swim::Render::GpuSkinHandle Instance;
			Swim::Render::RenderObjectHandle Object;
			Swim::Render::GpuMeshHandle Output;
			std::string Mesh;
			std::uint64_t PoseRevision = UINT64_MAX;
			std::vector<std::array<float, 12>> Previous;
			std::uint32_t MaterialSet = 0;
			std::uint32_t Flags = 0;
		};

		void UpdateLights(entt::registry& registry);
		void UpdateEmitters(entt::registry& registry);
		void UpdateSkins(entt::registry& registry);
		void ReleaseAll();
		void ReleaseLight(LightState& state);
		void ReleaseSkin(SkinState& state);
		std::uint32_t AllocateShadowSlot();

		FrameRenderer& renderer;
		Scene* scene = nullptr;
		std::uint64_t sceneId = 0;
		std::unique_ptr<RenderExtractor> extractor;
		RenderExtractionStats extraction;
		std::unordered_map<entt::entity, LightState> lights;
		std::unordered_map<entt::entity, EmitterState> emitters;
		std::unordered_map<entt::entity, SkinState> skins;
		std::unordered_map<std::string, SkinnedMeshState> skinnedMeshes;
		std::vector<bool> shadowSlots;
		std::vector<Swim::Render::ShadowCasterDesc> casters;
	};
} // namespace Engine
