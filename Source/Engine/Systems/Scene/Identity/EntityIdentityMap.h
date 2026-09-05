#pragma once

#include "SerializedEntityId.h"

#include <entt/entt.hpp>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <unordered_map>

namespace Engine
{

	class EntityIdentityMap
	{
	public:

		SerializedEntityId Assign(entt::entity entity)
		{
			if (entity == entt::null)
			{
				throw std::invalid_argument("Cannot assign a serialized identity to entt::null.");
			}

			if (const auto existing = entityToId.find(entity); existing != entityToId.end())
			{
				return existing->second;
			}

			while (nextValue == 0 || idToEntity.contains(SerializedEntityId{ nextValue }))
			{
				++nextValue;
			}

			const SerializedEntityId id{ nextValue++ };
			entityToId.emplace(entity, id);
			idToEntity.emplace(id, entity);
			return id;
		}

		bool Bind(entt::entity entity, SerializedEntityId id)
		{
			if (entity == entt::null || !id)
			{
				return false;
			}

			const auto existingEntity = entityToId.find(entity);
			if (existingEntity != entityToId.end())
			{
				return existingEntity->second == id;
			}

			const auto existingId = idToEntity.find(id);
			if (existingId != idToEntity.end())
			{
				return existingId->second == entity;
			}

			entityToId.emplace(entity, id);
			idToEntity.emplace(id, entity);
			if (id.Value >= nextValue)
			{
				nextValue = id.Value + 1;
				if (nextValue == 0)
				{
					nextValue = 1;
				}
			}
			return true;
		}

		bool Forget(entt::entity entity)
		{
			const auto existing = entityToId.find(entity);
			if (existing == entityToId.end())
			{
				return false;
			}

			idToEntity.erase(existing->second);
			entityToId.erase(existing);
			return true;
		}

		std::optional<SerializedEntityId> FindId(entt::entity entity) const
		{
			const auto existing = entityToId.find(entity);
			if (existing == entityToId.end())
			{
				return std::nullopt;
			}
			return existing->second;
		}

		std::optional<entt::entity> FindEntity(SerializedEntityId id) const
		{
			const auto existing = idToEntity.find(id);
			if (existing == idToEntity.end())
			{
				return std::nullopt;
			}
			return existing->second;
		}

		std::size_t GetCount() const { return entityToId.size(); }

		void Clear()
		{
			entityToId.clear();
			idToEntity.clear();
			nextValue = 1;
		}

	private:

		std::unordered_map<entt::entity, SerializedEntityId> entityToId;
		std::unordered_map<SerializedEntityId, entt::entity> idToEntity;
		std::uint64_t nextValue = 1;

	};

}
