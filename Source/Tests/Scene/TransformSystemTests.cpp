#include "Engine/Systems/Scene/TransformSystem.h"

#include <cstdint>
#include <iostream>

namespace
{

	int failures = 0;

	void Expect(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "[TransformSystemTests] " << message << '\n';
			++failures;
		}
	}

}

int main()
{
	Engine::TransformSystem transforms;
	std::uint64_t firstEpoch = 0;
	std::uint64_t secondEpoch = 0;
	const entt::entity first = static_cast<entt::entity>(1);
	const entt::entity second = static_cast<entt::entity>(2);

	Expect(!transforms.AreAnyTransformsDirty(), "new tracker should start clean");
	Expect(transforms.GetDirtyEntities().empty(), "new tracker should have no dirty entities");

	const std::uint64_t initialMutationVersion = transforms.GetMutationVersion();
	Expect(transforms.QueueDirty(first, firstEpoch), "first mutation in an epoch should queue");
	Expect(!transforms.QueueDirty(first, firstEpoch), "same entity should be deduplicated within an epoch");
	Expect(transforms.AreAnyTransformsDirty(), "queued mutation should mark tracker dirty");
	Expect(transforms.GetDirtyEntities().size() == 1, "same entity should appear once in dirty queue");
	Expect(transforms.GetMutationVersion() == initialMutationVersion + 1, "deduplicated mutation should advance version once");

	Expect(transforms.QueueDirty(second, secondEpoch), "a second entity should queue independently");
	Expect(transforms.GetDirtyEntities().size() == 2, "two unique entities should be tracked");
	Expect(transforms.GetMutationVersion() == initialMutationVersion + 2, "second entity should advance mutation version");

	const std::uint64_t previousEpoch = transforms.GetDirtyEpoch();
	transforms.BeginFrame();
	Expect(!transforms.AreAnyTransformsDirty(), "BeginFrame should clear dirty flag");
	Expect(transforms.GetDirtyEntities().empty(), "BeginFrame should clear dirty queue");
	Expect(transforms.GetDirtyEpoch() != previousEpoch, "BeginFrame should advance dirty epoch");
	Expect(transforms.QueueDirty(first, firstEpoch), "entity should be queueable again in a new frame");
	Expect(!transforms.QueueDirty(entt::null, secondEpoch), "null entity should never queue");

	if (failures != 0)
	{
		return 1;
	}

	return 0;
}
