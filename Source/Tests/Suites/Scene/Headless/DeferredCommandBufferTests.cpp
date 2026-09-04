#include "Engine/Systems/Scene/DeferredCommandBuffer.h"
#include "Tests/Framework/Test.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{

	struct TestContextState
	{
		std::vector<int> Values;
	};

}

SWIM_TEST("Scene.DeferredCommandBuffer", "CommandsFlushInEnqueueOrder")
{
	Engine::DeferredCommandBuffer<TestContextState> commands;
	TestContextState context;

	commands.Enqueue([](TestContextState& state) { state.Values.push_back(1); });
	commands.Enqueue([](TestContextState& state) { state.Values.push_back(2); });
	commands.Enqueue([](TestContextState& state) { state.Values.push_back(3); });

	SWIM_CHECK_EQUAL(commands.GetPendingCount(), std::size_t{ 3 });
	SWIM_CHECK_EQUAL(commands.Flush(context), std::size_t{ 3 });
	SWIM_CHECK(context.Values == std::vector<int>({ 1, 2, 3 }));
	SWIM_CHECK_EQUAL(commands.GetPendingCount(), std::size_t{ 0 });
	SWIM_CHECK(!commands.IsFlushing());
}

SWIM_TEST("Scene.DeferredCommandBuffer", "WorkQueuedDuringFlushDefersToTheNextFlush")
{
	Engine::DeferredCommandBuffer<TestContextState> commands;
	TestContextState context;

	commands.Enqueue([&commands](TestContextState& state)
	{
		state.Values.push_back(4);
		commands.Enqueue([](TestContextState& laterState) { laterState.Values.push_back(6); });
	});
	commands.Enqueue([](TestContextState& state) { state.Values.push_back(5); });

	SWIM_CHECK_EQUAL(commands.Flush(context), std::size_t{ 2 });
	SWIM_CHECK(context.Values == std::vector<int>({ 4, 5 }));
	SWIM_CHECK_EQUAL(commands.GetPendingCount(), std::size_t{ 1 });
	SWIM_CHECK_EQUAL(commands.Flush(context), std::size_t{ 1 });
	SWIM_CHECK(context.Values == std::vector<int>({ 4, 5, 6 }));
}

SWIM_TEST("Scene.DeferredCommandBuffer", "CommandsAreMoveOnlyAndMayOwnPayloads")
{
	Engine::DeferredCommandBuffer<TestContextState> commands;
	TestContextState context;

	commands.Enqueue([payload = std::make_unique<int>(7)](TestContextState& state)
	{
		state.Values.push_back(*payload);
	});

	SWIM_CHECK_EQUAL(commands.Flush(context), std::size_t{ 1 });
	SWIM_REQUIRE(!context.Values.empty());
	SWIM_CHECK_EQUAL(context.Values.back(), 7);
}

SWIM_TEST("Scene.DeferredCommandBuffer", "ClearDiscardsPendingWork")
{
	Engine::DeferredCommandBuffer<TestContextState> commands;
	TestContextState context;

	commands.Enqueue([](TestContextState& state) { state.Values.push_back(8); });
	commands.Clear();

	SWIM_CHECK_EQUAL(commands.GetPendingCount(), std::size_t{ 0 });
	SWIM_CHECK_EQUAL(commands.Flush(context), std::size_t{ 0 });
	SWIM_CHECK(context.Values.empty());
}

SWIM_TEST("Scene.DeferredCommandBuffer", "RecursiveFlushIsRejectedAndLeavesAUsableBuffer")
{
	Engine::DeferredCommandBuffer<TestContextState> commands;
	TestContextState context;

	commands.Enqueue([&commands](TestContextState& state)
	{
		state.Values.push_back(9);
		commands.Flush(state);
	});

	SWIM_CHECK_THROWS(commands.Flush(context), std::logic_error);
	SWIM_CHECK(!commands.IsFlushing());

	commands.Enqueue([](TestContextState& state) { state.Values.push_back(10); });
	SWIM_CHECK_EQUAL(commands.Flush(context), std::size_t{ 1 });
	SWIM_REQUIRE(!context.Values.empty());
	SWIM_CHECK_EQUAL(context.Values.back(), 10);
}

SWIM_TEST("Scene.DeferredCommandBuffer", "ConcurrentProducersLoseNoCommands")
{
	// Execution stays a single owner-thread flush; producer interleaving is
	// intentionally unspecified, so only the resulting set is checked.
	constexpr int ProducerCount = 4;
	constexpr int CommandsPerProducer = 64;

	Engine::DeferredCommandBuffer<TestContextState> commands;
	TestContextState context;

	std::atomic<int> ready{ 0 };
	std::vector<std::thread> producers;
	producers.reserve(ProducerCount);

	for (int producer = 0; producer < ProducerCount; ++producer)
	{
		producers.emplace_back([producer, &commands, &ready]()
		{
			++ready;
			while (ready.load() != ProducerCount)
			{
				std::this_thread::yield();
			}

			for (int command = 0; command < CommandsPerProducer; ++command)
			{
				const int value = 1000 + producer * CommandsPerProducer + command;
				commands.Enqueue([value](TestContextState& state) { state.Values.push_back(value); });
			}
		});
	}

	for (std::thread& producer : producers)
	{
		producer.join();
	}

	SWIM_CHECK_EQUAL(commands.GetPendingCount(), std::size_t{ ProducerCount * CommandsPerProducer });
	SWIM_CHECK_EQUAL(commands.Flush(context), std::size_t{ ProducerCount * CommandsPerProducer });
	SWIM_REQUIRE(context.Values.size() == static_cast<std::size_t>(ProducerCount * CommandsPerProducer));

	std::vector<int> sorted = context.Values;
	std::sort(sorted.begin(), sorted.end());

	bool everyValuePresent = true;
	for (int i = 0; i < ProducerCount * CommandsPerProducer; ++i)
	{
		if (sorted[static_cast<std::size_t>(i)] != 1000 + i)
		{
			everyValuePresent = false;
			break;
		}
	}
	SWIM_CHECK(everyValuePresent);
}
