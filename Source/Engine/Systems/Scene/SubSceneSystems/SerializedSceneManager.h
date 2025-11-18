#pragma once

#include "Library/EnTT/entt.hpp"
#include "Library/json/json.hpp"

namespace Engine
{

	// Saves a scene to JSON and Binary file formats, essentially is the Kernel between editor and engine.
	class SerializedSceneManager
	{

	public:

		explicit SerializedSceneManager(entt::registry& reg, const std::string& sceneName);

		void SendFullJSON();
		void SaveFullJSON();

	private:

		void BuildFullJSON();

		static std::wstring Utf8ToWide(const std::string& utf8);

		// Builds a JSON object for a single entity, calling per-component serializers.
		nlohmann::json BuildEntityJSON(entt::entity e, bool hasTag);

		void SerializeTransform(entt::entity e, nlohmann::json& jEntity);
		void SerializeMaterial(entt::entity e, nlohmann::json& jEntity);
		void SerializeTag(entt::entity e, nlohmann::json& jEntity);

		entt::registry& reg;

		nlohmann::json jsonRoot;

		std::string sceneName;

	};

};
