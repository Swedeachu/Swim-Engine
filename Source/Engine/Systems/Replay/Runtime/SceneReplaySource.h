#pragma once

#include "Engine/Systems/Replay/ReplayRecorder.h"
#include "Engine/Systems/Renderer/Particles/ParticleSettings.h"
#include "Engine/Systems/Renderer/Runtime/ProceduralMeshes.h"
#include "Engine/Systems/Renderer/Runtime/RenderServices.h"

#include <entt/entity/entity.hpp>

#include <memory>
#include <span>
#include <unordered_map>

namespace Engine
{

	class AudioSystem;
	class CameraSystem;
	class Scene;

	struct SceneReplaySourceDesc
	{
		Scene* Scene = nullptr;
		RenderServices Render;			 // Meshes and Materials are required.
		CameraSystem* Cameras = nullptr; // The main camera becomes node CameraNode.
		AudioSystem* Audio = nullptr;	 // Started sounds become "sound" events.
		bool SkipOverlay = true;		 // Leave out first-person overlay parts.
	};

	// Records an entity scene generically: one "mesh" node per MeshRenderer part (world
	// pose, assets [mesh, material]), "light" nodes for Light, "emitter" nodes for
	// ParticleEmitter, a node for every ReplayAnnotation, the main camera, and sounds.
	// Assets are keyed by their library path; procedural meshes and textures are cached
	// from the data they were registered with, so create the source before the game
	// registers runtime content (it observes MeshLibrary from construction). Materials
	// are stored as a small JSON description whose textures are assets of the recording.
	// Entities with ReplayIgnore (and their descendants) are left out.
	class SceneReplaySource final : public Swim::Replay::IReplayCaptureSource
	{

	  public:
		explicit SceneReplaySource(SceneReplaySourceDesc desc);

		~SceneReplaySource() override;

		SceneReplaySource(const SceneReplaySource&) = delete;

		SceneReplaySource& operator=(const SceneReplaySource&) = delete;

		void SetScene(Scene* scene);

		void Capture(Swim::Replay::ReplayCapture& capture) override;

		// Node ids: ((entity + 1) << 16) | slot. Mesh parts use slot = part index (< 0xFFF0).
		static Swim::Replay::NodeId EntityNode(entt::entity entity, std::uint32_t slot);

		static constexpr std::uint32_t LightSlot = 0xFFF1;
		static constexpr std::uint32_t EmitterSlot = 0xFFF2;
		static constexpr std::uint32_t AnnotationSlot = 0xFFF4;
		static constexpr Swim::Replay::NodeId CameraNode = 1;

	  private:
		struct State;

		std::unique_ptr<State> state;
	};

	// Asset payloads shared by the source and the presenter.
	namespace ReplayAssets
	{

		std::vector<std::uint8_t> EncodeMesh(const ProceduralMeshes::MeshData& mesh);

		bool DecodeMesh(std::span<const std::uint8_t> bytes, ProceduralMeshes::MeshData& mesh);

		std::vector<std::uint8_t> EncodeTexture(
			std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> rgba8, bool srgb, bool mips);

		struct TextureData
		{
			std::uint32_t Width = 0;
			std::uint32_t Height = 0;
			bool Srgb = true;
			bool Mips = true;
			std::vector<std::uint8_t> Rgba8;
		};

		bool DecodeTexture(std::span<const std::uint8_t> bytes, TextureData& texture);

		std::vector<std::uint8_t> EncodeEmitter(const Swim::Render::ParticleEmitterDesc& desc);

		bool DecodeEmitter(std::span<const std::uint8_t> bytes, Swim::Render::ParticleEmitterDesc& desc);

	} // namespace ReplayAssets

} // namespace Engine
