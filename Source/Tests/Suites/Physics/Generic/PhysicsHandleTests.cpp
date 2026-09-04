#include "Engine/Systems/Physics/Internal/GenerationalHandleTable.h"
#include "Engine/Systems/Physics/PhysicsHandles.h"
#include "Tests/Framework/Test.h"

#include <cstddef>
#include <string>

SWIM_TEST("Physics.GenerationalHandleTable", "InsertAndRemoveInvalidatesTheHandle")
{
	Engine::GenerationalHandleTable<Engine::BodyHandle, std::string> table;

	const Engine::BodyHandle first = table.Insert("first");
	SWIM_REQUIRE(static_cast<bool>(first));
	SWIM_CHECK_EQUAL(first.Generation, 1u);
	SWIM_CHECK(table.IsValid(first));
	SWIM_CHECK_EQUAL(table.Size(), std::size_t{ 1 });
	SWIM_REQUIRE(table.Get(first) != nullptr);
	SWIM_CHECK_EQUAL(*table.Get(first), std::string("first"));

	std::string removed;
	SWIM_CHECK(table.Remove(first, removed));
	SWIM_CHECK_EQUAL(removed, std::string("first"));
	SWIM_CHECK(!table.IsValid(first));
	SWIM_CHECK(table.Get(first) == nullptr);
	SWIM_CHECK_EQUAL(table.Size(), std::size_t{ 0 });
}

SWIM_TEST("Physics.GenerationalHandleTable", "SlotReuseAdvancesTheGeneration")
{
	Engine::GenerationalHandleTable<Engine::BodyHandle, std::string> table;

	const Engine::BodyHandle first = table.Insert("first");
	std::string removed;
	SWIM_REQUIRE(table.Remove(first, removed));

	const Engine::BodyHandle second = table.Insert("second");
	SWIM_REQUIRE(static_cast<bool>(second));
	SWIM_CHECK_EQUAL(second.Index, first.Index);
	SWIM_CHECK(second.Generation != first.Generation);
	SWIM_CHECK(table.IsValid(second));
	SWIM_CHECK(!table.IsValid(first));

	// A stale handle must never remove a live entry that reused its slot.
	std::string ignored;
	SWIM_CHECK(!table.Remove(first, ignored));
	SWIM_CHECK_EQUAL(table.Size(), std::size_t{ 1 });
}

SWIM_TEST("Physics.GenerationalHandleTable", "IterationVisitsOnlyLiveEntries")
{
	Engine::GenerationalHandleTable<Engine::BodyHandle, std::string> table;
	table.Insert("second");
	table.Insert("third");
	SWIM_REQUIRE(table.Size() == 2);

	std::size_t visited = 0;
	bool everyHandleValid = true;
	bool everyValueExpected = true;
	table.ForEach([&](Engine::BodyHandle handle, std::string& value)
	{
		everyHandleValid = everyHandleValid && table.IsValid(handle);
		everyValueExpected = everyValueExpected && (value == "second" || value == "third");
		++visited;
	});

	SWIM_CHECK_EQUAL(visited, std::size_t{ 2 });
	SWIM_CHECK(everyHandleValid);
	SWIM_CHECK(everyValueExpected);
}

SWIM_TEST("Physics.GenerationalHandleTable", "DefaultConstructedHandlesAreNeverValid")
{
	Engine::GenerationalHandleTable<Engine::BodyHandle, std::string> table;

	Engine::BodyHandle invalid{};
	SWIM_CHECK(!static_cast<bool>(invalid));
	SWIM_CHECK(!table.IsValid(invalid));
}
