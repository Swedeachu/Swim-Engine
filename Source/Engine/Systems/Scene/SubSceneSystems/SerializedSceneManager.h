#pragma once

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Swim::Platform
{
	class FileSystem;
}

namespace Engine
{

	class MaterialPool;

	// Saves a scene to JSON and Binary file formats, essentially is the Kernel between editor and engine.
	class SerializedSceneManager
	{

	public:

		using EditorMessageCallback = std::function<bool(const std::string&, std::uintptr_t)>;

		explicit SerializedSceneManager(entt::registry& reg, const std::string& sceneName, MaterialPool& materials, Swim::Platform::FileSystem& files, EditorMessageCallback sendEditorMessage = {});

		void SendFullJSON();
		void SaveFullJSON();

		// Public interface used by Scene code (no behavior change at callsites):
		// these now queue up changes internally, instead of sending immediately.
		void SendEntityCreated(entt::entity e);
		void SendEntityDestroyed(entt::entity e);
		void SendEntityUpdated(entt::entity e);

		// Called once per frame (e.g. from Scene::InternalScenePostUpdate)
		// to flush any queued changes as a single "scene sync:" message.
		void SendSync();

	private:

		void BuildFullJSON();


		// Builds a JSON object for a single entity, calling per-component serializers.
		nlohmann::json BuildEntityJSON(entt::entity e);

		void SerializeTransform(entt::entity e, nlohmann::json& jEntity);
		void SerializeMaterial(entt::entity e, nlohmann::json& jEntity);
		void SerializeTag(entt::entity e, nlohmann::json& jEntity);

		// Internal helpers for queuing entities for sync.
		void EnqueueCreated(entt::entity e);
		void EnqueueUpdated(entt::entity e);
		void EnqueueDestroyed(entt::entity e);

		const bool ShouldSerialize(entt::entity e) const;

		entt::registry& reg;
		MaterialPool* materials = nullptr;
		Swim::Platform::FileSystem* files = nullptr;
		EditorMessageCallback sendEditorMessage;

		nlohmann::json jsonRoot;

		std::string sceneName;

		// Per-frame diff queues (cleared after SendSync).
		std::vector<entt::entity> createdEntities;
		std::vector<entt::entity> updatedEntities;
		std::vector<entt::entity> destroyedEntities;

	};

};
