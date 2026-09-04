#pragma once

#include "EntityIdentityMap.h"

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <utility>

namespace Engine
{

	class SceneSerializer
	{
	public:

		static constexpr std::uint32_t SchemaVersion = 1;

		SceneSerializer(entt::registry& registry, const EntityIdentityMap& identities, std::string sceneName)
			: registry(&registry), identities(&identities), sceneName(std::move(sceneName))
		{}

		nlohmann::json SerializeScene() const;
		nlohmann::json SerializeEntity(entt::entity entity) const;

		bool ShouldSerialize(entt::entity entity) const;
		entt::entity FindEntity(SerializedEntityId id) const;
		SerializedEntityId FindId(entt::entity entity) const;
		const std::string& GetSceneName() const { return sceneName; }

	private:

		void SerializeTransform(entt::entity entity, nlohmann::json& jsonEntity) const;
		void SerializeRenderable(entt::entity entity, nlohmann::json& jsonEntity) const;
		void SerializeTag(entt::entity entity, nlohmann::json& jsonEntity) const;

		entt::registry* registry = nullptr;
		const EntityIdentityMap* identities = nullptr;
		std::string sceneName;

	};

}
