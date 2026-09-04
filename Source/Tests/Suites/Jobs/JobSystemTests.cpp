#include "Engine/Jobs/JobSystem.h"
#include "Engine/Memory/ScratchArena.h"
#include "Tests/Framework/Test.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

namespace
{

	// Every case needs a live scheduler, and the job system is explicit about its
	// own lifecycle, so a small scoped helper keeps shutdown correct even when a
	// requirement aborts a case early.
	class ScopedJobSystem
	{

	public:

		explicit ScopedJobSystem(
			std::uint32_t workerThreads = 2,
			std::uint32_t blockingThreads = 1,
			std::uint32_t externalThreads = 0)
		{
			Swim::Jobs::JobSystemDesc desc{};
			desc.WorkerThreads = workerThreads;
			desc.BlockingThreads = blockingThreads;
			desc.ExternalThreads = externalThreads;
			initialized = jobs.Initialize(desc);
		}

		~ScopedJobSystem()
		{
			if (initialized && jobs.IsRunning())
			{
				jobs.Shutdown();
			}
		}

		ScopedJobSystem(const ScopedJobSystem&) = delete;
		ScopedJobSystem& operator=(const ScopedJobSystem&) = delete;

		bool IsInitialized() const
		{
			return initialized;
		}

		Swim::Jobs::JobSystem& Get()
		{
			return jobs;
		}

	private:

		Swim::Jobs::JobSystem jobs;
		bool initialized = false;

	};

}

SWIM_TEST("Jobs.JobSystem", "InitializeReservesEveryWorkerSlot")
{
	ScopedJobSystem scoped;
	SWIM_REQUIRE(scoped.IsInitialized());
	SWIM_CHECK_EQUAL(scoped.Get().GetWorkerSlotCount(), std::uint32_t{ 3 });
}

SWIM_TEST("Jobs.JobSystem", "DependenciesOrderExecution")
{
	ScopedJobSystem scoped;
	SWIM_REQUIRE(scoped.IsInitialized());
	Swim::Jobs::JobSystem& jobs = scoped.Get();

	std::atomic<int> dependencyValue{ 0 };
	auto first = jobs.CreateJob([&](std::uint32_t)
	{
		dependencyValue.store(7, std::memory_order_release);
	});
	auto second = jobs.CreateJob([&](std::uint32_t)
	{
		SWIM_CHECK_EQUAL(dependencyValue.load(std::memory_order_acquire), 7);
		dependencyValue.store(11, std::memory_order_release);
	});

	jobs.AddDependency(second, first);
	jobs.Submit(first);
	jobs.Wait(second);

	SWIM_CHECK_EQUAL(dependencyValue.load(std::memory_order_acquire), 11);
}

SWIM_TEST("Jobs.JobSystem", "ParallelForCoversEveryElement")
{
	ScopedJobSystem scoped;
	SWIM_REQUIRE(scoped.IsInitialized());

	std::vector<int> values(4096, 0);
	scoped.Get().ParallelFor(values.size(), 64, [&](std::size_t begin, std::size_t end, std::uint32_t)
	{
		for (std::size_t i = begin; i < end; ++i)
		{
			values[i] = static_cast<int>(i);
		}
	});

	bool everyElementWritten = true;
	for (std::size_t i = 0; i < values.size(); ++i)
	{
		if (values[i] != static_cast<int>(i))
		{
			everyElementWritten = false;
			break;
		}
	}
	SWIM_CHECK(everyElementWritten);
}

SWIM_TEST("Jobs.JobSystem", "MainThreadJobsRunWithRewoundScratch")
{
	ScopedJobSystem scoped;
	SWIM_REQUIRE(scoped.IsInitialized());
	Swim::Jobs::JobSystem& jobs = scoped.Get();

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

	SWIM_CHECK(mainThreadRan);
	SWIM_CHECK(mainThreadScratchAvailable);
	SWIM_CHECK_EQUAL(mainScratch.GetStats().UsedBytes, mainScratchBaseline);
}

SWIM_TEST("Jobs.JobSystem", "TaskGroupsSubmitAndWaitTogether")
{
	ScopedJobSystem scoped;
	SWIM_REQUIRE(scoped.IsInitialized());
	Swim::Jobs::JobSystem& jobs = scoped.Get();

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

	SWIM_CHECK_EQUAL(roots.Size(), std::size_t{ 2 });
	jobs.Submit(roots);
	jobs.Wait(roots);
	SWIM_CHECK_EQUAL(groupValue.load(std::memory_order_relaxed), 2);
}

SWIM_TEST("Jobs.JobSystem", "CancelledJobsSkipUserWork")
{
	ScopedJobSystem scoped;
	SWIM_REQUIRE(scoped.IsInitialized());
	Swim::Jobs::JobSystem& jobs = scoped.Get();

	bool cancelledRan = false;
	auto cancelled = jobs.CreateJob([&](std::uint32_t)
	{
		cancelledRan = true;
	});

	jobs.Cancel(cancelled);
	jobs.Submit(cancelled);
	jobs.Wait(cancelled);

	SWIM_CHECK(cancelled.IsCancellationRequested());
	SWIM_CHECK(!cancelledRan);
}

SWIM_TEST("Jobs.JobSystem", "BlockingLaneRunsScheduledWork")
{
	ScopedJobSystem scoped;
	SWIM_REQUIRE(scoped.IsInitialized());
	Swim::Jobs::JobSystem& jobs = scoped.Get();

	bool blockingRan = false;
	auto blocking = jobs.ScheduleBlocking([&]()
	{
		blockingRan = true;
	});
	jobs.Wait(blocking);
	SWIM_CHECK(blockingRan);
}

SWIM_TEST("Jobs.JobSystem", "ExternalThreadsRegisterIntoReservedSlots")
{
	// Registration claims one of the slots reserved through JobSystemDesc, and
	// it must be done from a thread the scheduler does not already own. The
	// owning thread is scheduler thread 0 and registering it as external would
	// discard that identity.
	ScopedJobSystem reserved(2, 1, 1);
	SWIM_REQUIRE(reserved.IsInitialized());
	Swim::Jobs::JobSystem& jobs = reserved.Get();

	bool registered = false;
	bool ranWorkWhileRegistered = false;

	std::thread external([&]()
	{
		registered = jobs.RegisterCurrentExternalThread();
		if (!registered)
		{
			return;
		}

		auto job = jobs.CreateJob([&](std::uint32_t)
		{
			ranWorkWhileRegistered = true;
		});
		jobs.Submit(job);
		jobs.Wait(job);

		jobs.UnregisterCurrentExternalThread();
	});
	external.join();

	SWIM_CHECK(registered);
	SWIM_CHECK(ranWorkWhileRegistered);
}

SWIM_TEST("Jobs.JobSystem", "ExternalThreadRegistrationFailsWithoutReservedSlots")
{
	ScopedJobSystem unreserved(2, 1, 0);
	SWIM_REQUIRE(unreserved.IsInitialized());
	Swim::Jobs::JobSystem& jobs = unreserved.Get();

	bool registered = true;
	std::thread external([&]()
	{
		registered = jobs.RegisterCurrentExternalThread();
		if (registered)
		{
			jobs.UnregisterCurrentExternalThread();
		}
	});
	external.join();

	SWIM_CHECK(!registered);
}

SWIM_TEST("Jobs.JobSystem", "ShutdownDrainsOutstandingWork")
{
	ScopedJobSystem scoped;
	SWIM_REQUIRE(scoped.IsInitialized());
	Swim::Jobs::JobSystem& jobs = scoped.Get();

	jobs.WaitForAll();
	jobs.Shutdown();
	SWIM_CHECK(!jobs.IsRunning());
}
