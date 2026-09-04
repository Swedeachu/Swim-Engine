#pragma once

#include <cstdint>
#include <vector>

#include <entt/entity/entity.hpp>

namespace Engine
{

	// Scene-owned transform mutation tracking. Transform components keep only a
	// non-owning pointer to the tracker for the scene/registry that owns them.
	class TransformSystem
	{

	public:

		void BeginFrame()
		{
			dirtyEntities.clear();
			transformsDirty = false;

			++dirtyEpoch;
			if (dirtyEpoch == 0)
			{
				dirtyEpoch = 1;
			}
		}

		bool QueueDirty(entt::entity entity, std::uint64_t& lastQueuedEpoch)
		{
			if (entity == entt::null)
			{
				return false;
			}

			transformsDirty = true;
			if (lastQueuedEpoch == dirtyEpoch)
			{
				return false;
			}

			lastQueuedEpoch = dirtyEpoch;
			dirtyEntities.push_back(entity);

			++mutationVersion;
			if (mutationVersion == 0)
			{
				mutationVersion = 1;
			}

			return true;
		}

		bool AreAnyTransformsDirty() const { return transformsDirty; }
		const std::vector<entt::entity>& GetDirtyEntities() const { return dirtyEntities; }
		std::uint64_t GetMutationVersion() const { return mutationVersion; }
		std::uint64_t GetDirtyEpoch() const { return dirtyEpoch; }

	private:

		bool transformsDirty = false;
		std::vector<entt::entity> dirtyEntities;
		std::uint64_t dirtyEpoch = 1;
		std::uint64_t mutationVersion = 1;

	};

}
