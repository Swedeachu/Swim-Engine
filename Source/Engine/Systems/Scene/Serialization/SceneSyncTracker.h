#pragma once

#include "SceneSerializer.h"
#include "SceneToolingBridge.h"

#include <entt/entt.hpp>

#include <unordered_set>
#include <vector>

namespace Engine
{

	class SceneSyncTracker
	{
	public:

		SceneSyncTracker(SceneSerializer& serializer, SceneToolingBridge& tooling)
			: serializer(&serializer), tooling(&tooling)
		{
			PrimeKnownEntities();
		}

		void EntityMutated(entt::entity entity);
		void EntityDestroyed(SerializedEntityId id);

		void SendFullScene() const;
		void Flush();
		void Clear();

	private:

		void PrimeKnownEntities();
		void EnqueueCreated(SerializedEntityId id);
		void EnqueueUpdated(SerializedEntityId id);
		void EnqueueDestroyed(SerializedEntityId id);

		SceneSerializer* serializer = nullptr;
		SceneToolingBridge* tooling = nullptr;

		std::unordered_set<SerializedEntityId> knownEntities;
		std::unordered_set<SerializedEntityId> destroyingEntities;
		std::unordered_set<SerializedEntityId> dirtyEntitySet;
		std::vector<SerializedEntityId> dirtyEntities;
		std::vector<SerializedEntityId> createdEntities;
		std::vector<SerializedEntityId> updatedEntities;
		std::vector<SerializedEntityId> destroyedEntities;

	};

}
