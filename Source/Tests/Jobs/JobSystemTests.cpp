#include "Engine/Jobs/JobSystem.h"
#include "Engine/Memory/ScratchArena.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace
{

	void Require(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "JobSystem test failed: " << message << '\n';
			std::exit(1);
		}
	}

}

int main()
{
	Swim::Jobs::JobSystem jobs;
	Swim::Jobs::JobSystemDesc desc{};
	desc.WorkerThreads = 2;
	desc.BlockingThreads = 1;
	Require(jobs.Initialize(desc), "Initialize");
	Require(jobs.GetWorkerSlotCount() == 3, "worker slot count");

	std::atomic<int> dependencyValue{ 0 };
	auto first = jobs.CreateJob([&](std::uint32_t)
	{
		dependencyValue.store(7, std::memory_order_release);
	});
	auto second = jobs.CreateJob([&](std::uint32_t)
	{
		Require(dependencyValue.load(std::memory_order_acquire) == 7, "dependency ordering");
		dependencyValue.store(11, std::memory_order_release);
	});
	jobs.AddDependency(second, first);
	jobs.Submit(first);
	jobs.Wait(second);
	Require(dependencyValue.load(std::memory_order_acquire) == 11, "dependent completion");

	std::vector<int> values(4096, 0);
	jobs.ParallelFor(values.size(), 64, [&](std::size_t begin, std::size_t end, std::uint32_t)
	{
		for (std::size_t i = begin; i < end; ++i)
		{
			values[i] = static_cast<int>(i);
		}
	});
	for (std::size_t i = 0; i < values.size(); ++i)
	{
		Require(values[i] == static_cast<int>(i), "parallel-for result");
	}

	auto& mainScratch = Swim::Memory::GetThreadScratchArena();
	mainScratch.Reset();
	mainScratch.Allocate(32);
	const std::size_t mainScratchBaseline = mainScratch.GetStats().UsedBytes;

	bool mainThreadRan = false;
	bool mainThreadScratchAvailable = false;
	auto mainJob = jobs.ScheduleMainThread([&]()
	{
		mainThreadRan = true;
		auto& scratch = Swim::Memory::GetThreadScratchArena();
		scratch.Allocate(128);
		mainThreadScratchAvailable = scratch.GetStats().UsedBytes > mainScratchBaseline;
	});
	jobs.RunMainThreadJobs();
	jobs.Wait(mainJob);
	Require(mainThreadRan, "main-thread pinned task");
	Require(mainThreadScratchAvailable, "main-thread job scratch available");
	Require(mainScratch.GetStats().UsedBytes == mainScratchBaseline, "main-thread job scratch rewound");

	std::atomic<int> groupValue{ 0 };
	Swim::Jobs::TaskGroup roots;
	roots.Add(jobs.CreateJob([&](std::uint32_t)
	{
		groupValue.fetch_add(1, std::memory_order_relaxed);
	}, Swim::Jobs::JobPriority::High));
	roots.Add(jobs.CreateJob([&](std::uint32_t)
	{
		groupValue.fetch_add(1, std::memory_order_relaxed);
	}, Swim::Jobs::JobPriority::Low));
	Require(roots.Size() == 2, "task group size");
	jobs.Submit(roots);
	jobs.Wait(roots);
	Require(groupValue.load(std::memory_order_relaxed) == 2, "task group execution");

	bool cancelledRan = false;
	auto cancelled = jobs.CreateJob([&](std::uint32_t)
	{
		cancelledRan = true;
	});
	jobs.Cancel(cancelled);
	jobs.Submit(cancelled);
	jobs.Wait(cancelled);
	Require(cancelled.IsCancellationRequested(), "cancellation request state");
	Require(!cancelledRan, "cancelled job does not execute user work");

	bool blockingRan = false;
	auto blocking = jobs.ScheduleBlocking([&]()
	{
		blockingRan = true;
	});
	jobs.Wait(blocking);
	Require(blockingRan, "blocking-lane task");

	Require(jobs.RegisterCurrentExternalThread(), "external-thread registration");
	jobs.UnregisterCurrentExternalThread();

	jobs.WaitForAll();
	jobs.Shutdown();
	Require(!jobs.IsRunning(), "shutdown");
	return 0;
}
