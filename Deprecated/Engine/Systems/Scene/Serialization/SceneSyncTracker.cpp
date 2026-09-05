#include "PCH.h"
#include "SceneSyncTracker.h"

#include <algorithm>

namespace Engine
{

	using nlohmann::json;

	void SceneSyncTracker::PrimeKnownEntities()
	{
		const json document = serializer->SerializeScene();
		if (!document.contains("entities") || !document["entities"].is_array())
		{
			return;
		}

		for (const json& entity : document["entities"])
		{
			if (!entity.contains("id") || !entity["id"].is_number_unsigned())
			{
				continue;
			}
			knownEntities.insert(SerializedEntityId{ entity["id"].get<std::uint64_t>() });
		}
	}

	void SceneSyncTracker::EntityMutated(entt::entity entity)
	{
		const SerializedEntityId id = serializer->FindId(entity);
		if (!id || destroyingEntities.contains(id))
		{
			return;
		}

		if (dirtyEntitySet.insert(id).second)
		{
			dirtyEntities.push_back(id);
		}
	}

	void SceneSyncTracker::EntityDestroyed(SerializedEntityId id)
	{
		if (!id)
		{
			return;
		}

		destroyingEntities.insert(id);
		dirtyEntitySet.erase(id);
		if (!knownEntities.erase(id))
		{
			return;
		}

		EnqueueDestroyed(id);
	}

	void SceneSyncTracker::SendFullScene() const
	{
		if (!tooling->IsConnected())
		{
			return;
		}
		tooling->Send("scene load:" + serializer->SerializeScene().dump(), 2);
	}

	void SceneSyncTracker::ResynchronizeFullScene()
	{
		Clear();
		knownEntities.clear();
		PrimeKnownEntities();
		SendFullScene();
	}

	void SceneSyncTracker::Flush()
	{
		// Do not pay JSON construction/serialization costs when there is no real
		// editor endpoint. A callback object exists in standalone runs too, so this
		// must use SceneToolingBridge's actual connection state.
		if (!tooling->IsConnected())
		{
			Clear();
			return;
		}

		for (SerializedEntityId id : dirtyEntities)
		{
			if (!dirtyEntitySet.contains(id) || destroyingEntities.contains(id))
			{
				continue;
			}

			const entt::entity entity = serializer->FindEntity(id);
			const bool shouldSerialize = entity != entt::null && serializer->ShouldSerialize(entity);
			const bool known = knownEntities.contains(id);

			if (!shouldSerialize)
			{
				if (known)
				{
					knownEntities.erase(id);
					EnqueueDestroyed(id);
				}
				continue;
			}

			if (!known)
			{
				knownEntities.insert(id);
				EnqueueCreated(id);
			}
			else
			{
				EnqueueUpdated(id);
			}
		}

		dirtyEntities.clear();
		dirtyEntitySet.clear();

		if (createdEntities.empty() && updatedEntities.empty() && destroyedEntities.empty())
		{
			destroyingEntities.clear();
			return;
		}

		json syncRoot = json::object();
		syncRoot["schemaVersion"] = SceneSerializer::SchemaVersion;
		syncRoot["scene"] = serializer->GetSceneName();
		syncRoot["created"] = json::array();
		syncRoot["updated"] = json::array();
		syncRoot["destroyed"] = json::array();

		for (SerializedEntityId id : createdEntities)
		{
			const entt::entity entity = serializer->FindEntity(id);
			if (entity == entt::null || !serializer->ShouldSerialize(entity))
			{
				continue;
			}
			json serialized = serializer->SerializeEntity(entity);
			if (!serialized.is_null())
			{
				syncRoot["created"].push_back(std::move(serialized));
			}
		}

		for (SerializedEntityId id : updatedEntities)
		{
			const entt::entity entity = serializer->FindEntity(id);
			if (entity == entt::null || !serializer->ShouldSerialize(entity))
			{
				continue;
			}
			json serialized = serializer->SerializeEntity(entity);
			if (!serialized.is_null())
			{
				syncRoot["updated"].push_back(std::move(serialized));
			}
		}

		for (SerializedEntityId id : destroyedEntities)
		{
			syncRoot["destroyed"].push_back({ { "id", id.Value } });
		}

		if (tooling->IsConnected())
		{
			tooling->Send("scene sync:" + syncRoot.dump(), 2);
		}

		Clear();
	}

	void SceneSyncTracker::Clear()
	{
		dirtyEntities.clear();
		dirtyEntitySet.clear();
		createdEntities.clear();
		updatedEntities.clear();
		destroyedEntities.clear();
		destroyingEntities.clear();
	}

	void SceneSyncTracker::EnqueueCreated(SerializedEntityId id)
	{
		destroyedEntities.erase(std::remove(destroyedEntities.begin(), destroyedEntities.end(), id), destroyedEntities.end());
		updatedEntities.erase(std::remove(updatedEntities.begin(), updatedEntities.end(), id), updatedEntities.end());
		if (std::find(createdEntities.begin(), createdEntities.end(), id) == createdEntities.end())
		{
			createdEntities.push_back(id);
		}
	}

	void SceneSyncTracker::EnqueueUpdated(SerializedEntityId id)
	{
		if (std::find(createdEntities.begin(), createdEntities.end(), id) != createdEntities.end())
		{
			return;
		}
		if (std::find(destroyedEntities.begin(), destroyedEntities.end(), id) != destroyedEntities.end())
		{
			return;
		}
		if (std::find(updatedEntities.begin(), updatedEntities.end(), id) == updatedEntities.end())
		{
			updatedEntities.push_back(id);
		}
	}

	void SceneSyncTracker::EnqueueDestroyed(SerializedEntityId id)
	{
		const auto created = std::find(createdEntities.begin(), createdEntities.end(), id);
		if (created != createdEntities.end())
		{
			createdEntities.erase(created);
			updatedEntities.erase(std::remove(updatedEntities.begin(), updatedEntities.end(), id), updatedEntities.end());
			return;
		}

		updatedEntities.erase(std::remove(updatedEntities.begin(), updatedEntities.end(), id), updatedEntities.end());
		if (std::find(destroyedEntities.begin(), destroyedEntities.end(), id) == destroyedEntities.end())
		{
			destroyedEntities.push_back(id);
		}
	}

}
