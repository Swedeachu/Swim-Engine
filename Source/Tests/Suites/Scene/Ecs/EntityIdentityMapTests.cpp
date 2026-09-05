#include "Engine/Systems/Scene/Identity/EntityIdentityMap.h"
#include "Tests/Framework/Test.h"

#include <cstddef>
#include <cstdint>

SWIM_TEST("Scene.EntityIdentityMap", "AssignedIdentitiesAreStableAndUnique")
{
	entt::registry registry;
	Engine::EntityIdentityMap identities;

	const entt::entity first = registry.create();
	const entt::entity second = registry.create();

	const Engine::SerializedEntityId firstId = identities.Assign(first);
	const Engine::SerializedEntityId secondId = identities.Assign(second);

	SWIM_CHECK(firstId.IsValid());
	SWIM_CHECK(secondId.IsValid());
	SWIM_CHECK(firstId != secondId);
	SWIM_CHECK(identities.Assign(first) == firstId);
	SWIM_REQUIRE(identities.FindEntity(firstId).has_value());
	SWIM_CHECK(identities.FindEntity(firstId).value() == first);
	SWIM_REQUIRE(identities.FindId(second).has_value());
	SWIM_CHECK(identities.FindId(second).value() == secondId);
}

SWIM_TEST("Scene.EntityIdentityMap", "ForgetRemovesBothDirections")
{
	entt::registry registry;
	Engine::EntityIdentityMap identities;

	const entt::entity first = registry.create();
	const Engine::SerializedEntityId firstId = identities.Assign(first);

	SWIM_CHECK(identities.Forget(first));
	SWIM_CHECK(!identities.FindEntity(firstId).has_value());
	SWIM_CHECK(!identities.FindId(first).has_value());
}

SWIM_TEST("Scene.EntityIdentityMap", "RestoredBindingsAdvanceTheNextAssignedId")
{
	entt::registry registry;
	Engine::EntityIdentityMap identities;

	const entt::entity restored = registry.create();
	const Engine::SerializedEntityId restoredId{ 4096 };
	SWIM_REQUIRE(identities.Bind(restored, restoredId));
	SWIM_REQUIRE(identities.FindEntity(restoredId).has_value());
	SWIM_CHECK(identities.FindEntity(restoredId).value() == restored);

	const entt::entity afterRestore = registry.create();
	SWIM_CHECK_EQUAL(identities.Assign(afterRestore).Value, restoredId.Value + 1);
}

SWIM_TEST("Scene.EntityIdentityMap", "InvalidBindingsAreRejected")
{
	entt::registry registry;
	Engine::EntityIdentityMap identities;

	const entt::entity restored = registry.create();
	const Engine::SerializedEntityId restoredId{ 4096 };
	SWIM_REQUIRE(identities.Bind(restored, restoredId));

	const entt::entity collision = registry.create();
	SWIM_CHECK(!identities.Bind(collision, restoredId));
	SWIM_CHECK(!identities.Bind(entt::null, Engine::SerializedEntityId{ 7 }));
	SWIM_CHECK(!identities.Bind(collision, Engine::SerializedEntityId{}));
}

SWIM_TEST("Scene.EntityIdentityMap", "ClearResetsTheIdentityCounter")
{
	entt::registry registry;
	Engine::EntityIdentityMap identities;
	identities.Assign(registry.create());
	identities.Assign(registry.create());

	identities.Clear();
	SWIM_CHECK_EQUAL(identities.GetCount(), std::size_t{ 0 });

	const entt::entity reset = registry.create();
	SWIM_CHECK_EQUAL(identities.Assign(reset).Value, std::uint64_t{ 1 });
}
