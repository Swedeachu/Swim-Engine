#include "Engine/Systems/Scene/TransformSystem.h"
#include "Tests/Framework/Test.h"

#include <cstddef>
#include <cstdint>

SWIM_TEST("Scene.TransformSystem", "NewTrackerStartsClean")
{
	Engine::TransformSystem transforms;
	SWIM_CHECK(!transforms.AreAnyTransformsDirty());
	SWIM_CHECK(transforms.GetDirtyEntities().empty());
}

SWIM_TEST("Scene.TransformSystem", "MutationsDeduplicateWithinAnEpoch")
{
	Engine::TransformSystem transforms;
	std::uint64_t firstEpoch = 0;
	const entt::entity first = static_cast<entt::entity>(1);

	const std::uint64_t initialMutationVersion = transforms.GetMutationVersion();
	SWIM_CHECK(transforms.QueueDirty(first, firstEpoch));
	SWIM_CHECK(!transforms.QueueDirty(first, firstEpoch));
	SWIM_CHECK(transforms.AreAnyTransformsDirty());
	SWIM_CHECK_EQUAL(transforms.GetDirtyEntities().size(), std::size_t{ 1 });
	SWIM_CHECK_EQUAL(transforms.GetMutationVersion(), initialMutationVersion + 1);
}

SWIM_TEST("Scene.TransformSystem", "EntitiesAreTrackedIndependently")
{
	Engine::TransformSystem transforms;
	std::uint64_t firstEpoch = 0;
	std::uint64_t secondEpoch = 0;
	const entt::entity first = static_cast<entt::entity>(1);
	const entt::entity second = static_cast<entt::entity>(2);

	const std::uint64_t initialMutationVersion = transforms.GetMutationVersion();
	SWIM_CHECK(transforms.QueueDirty(first, firstEpoch));
	SWIM_CHECK(transforms.QueueDirty(second, secondEpoch));
	SWIM_CHECK_EQUAL(transforms.GetDirtyEntities().size(), std::size_t{ 2 });
	SWIM_CHECK_EQUAL(transforms.GetMutationVersion(), initialMutationVersion + 2);
}

SWIM_TEST("Scene.TransformSystem", "BeginFrameClearsStateAndAdvancesTheEpoch")
{
	Engine::TransformSystem transforms;
	std::uint64_t firstEpoch = 0;
	const entt::entity first = static_cast<entt::entity>(1);
	SWIM_REQUIRE(transforms.QueueDirty(first, firstEpoch));

	const std::uint64_t previousEpoch = transforms.GetDirtyEpoch();
	transforms.BeginFrame();

	SWIM_CHECK(!transforms.AreAnyTransformsDirty());
	SWIM_CHECK(transforms.GetDirtyEntities().empty());
	SWIM_CHECK(transforms.GetDirtyEpoch() != previousEpoch);
	SWIM_CHECK(transforms.QueueDirty(first, firstEpoch));
}

SWIM_TEST("Scene.TransformSystem", "NullEntitiesNeverQueue")
{
	Engine::TransformSystem transforms;
	std::uint64_t epoch = 0;
	SWIM_CHECK(!transforms.QueueDirty(entt::null, epoch));
}
