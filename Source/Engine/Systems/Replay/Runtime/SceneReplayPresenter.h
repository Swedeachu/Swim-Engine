#pragma once

#include "Engine/Systems/Replay/ReplayPlayer.h"
#include "Engine/Systems/Renderer/Runtime/MeshLibrary.h"
#include "Engine/Systems/Renderer/Runtime/RenderServices.h"

#include <entt/entity/entity.hpp>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace Swim::Commands
{
	class CommandRegistry;
}

namespace Engine
{

	class AudioSystem;
	class CameraSystem;
	class Scene;

	struct SceneReplayPresenterDesc
	{
		Scene* Scene = nullptr; // Usually an empty scene kept for playback.
		RenderServices Render;	// Meshes and Materials are required.
		Swim::Replay::ReplayPlayer* Player = nullptr;
		CameraSystem* Cameras = nullptr;					 // Follows the player's camera.
		AudioSystem* Audio = nullptr;						 // Plays "sound" events.
		Swim::Commands::CommandRegistry* Commands = nullptr; // Environment values set console variables of the same name.

		// Optional: game resolution of asset keys, tried before the libraries. Return an
		// invalid handle (or InvalidIndex) to fall back.
		std::function<MeshLibrary::MeshHandle(const Swim::Replay::AssetRecord&)> ResolveMesh;
		std::function<std::uint32_t(const Swim::Replay::AssetRecord&)> ResolveMaterial;
		// Optional: replaces the console-variable mapping of environment values.
		std::function<void(const std::map<std::string, Swim::Replay::Value>&)> Environment;
	};

	// Rebuilds a recording made by SceneReplaySource (or any source using the same node
	// kinds) as entities: "mesh" nodes become MeshRenderer entities, "light" nodes Light,
	// "emitter" nodes ParticleEmitter; sounds play through the AudioSystem and the camera
	// follows the player. Other kinds go to presenters registered with RegisterKind and are
	// otherwise ignored. Use with ReplayPlayer::Present every frame.
	class SceneReplayPresenter final : public Swim::Replay::IReplayPresenter
	{

	  public:
		explicit SceneReplayPresenter(SceneReplayPresenterDesc desc);

		// ReleaseAssets()s.
		~SceneReplayPresenter() override;

		SceneReplayPresenter(const SceneReplayPresenter&) = delete;

		SceneReplayPresenter& operator=(const SceneReplayPresenter&) = delete;

		// Nodes and events of this kind go to presenter instead (terrain, skinned actors...).
		void RegisterKind(std::string_view kind, Swim::Replay::IReplayPresenter& presenter);

		void UnregisterKind(std::string_view kind);

		// Destroys the presented entities and restores the camera, depth of field and the
		// console variables it changed. Call ReplayPlayer::ResetPresented before presenting again.
		void Clear();

		// Clear()s, then unregisters the meshes and textures it registered from cached
		// recording data and releases the materials it created (call when a replay closes;
		// the next Present resolves everything again).
		void ReleaseAssets();

		// The entity presenting a node (entt::null if none).
		entt::entity FindEntity(Swim::Replay::NodeId id) const;

		void OnAdd(const Swim::Replay::NodeState& node) override;

		void OnUpdate(const Swim::Replay::NodeState& node, const Swim::Replay::NodeState& before) override;

		void OnRemove(const Swim::Replay::NodeState& node) override;

		void OnEvent(const Swim::Replay::ReplayEvent& event) override;

		void OnCamera(const Swim::Replay::CameraPose& pose) override;

		void OnEnvironment(const std::map<std::string, Swim::Replay::Value>& environment) override;

	  private:
		struct State;

		std::unique_ptr<State> state;
	};

} // namespace Engine
