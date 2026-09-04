#include "JobSystem.h"
#include "Engine/Memory/ScratchArena.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>

#if SWIM_JOBS_USE_ENKITS
#include <TaskScheduler.h>
#endif

namespace Swim::Jobs
{

	namespace Detail
	{

		enum class JobKind : std::uint8_t
		{
			Task,
			PinnedMain,
			PinnedBlocking
		};

		struct JobState
		{
			JobKind Kind = JobKind::Task;
			std::atomic<bool> Active{ false };
			std::atomic<bool> CancelRequested{ false };
			std::atomic<bool> Tracked{ false };
			std::vector<std::shared_ptr<JobState>> Parents;
			std::vector<std::weak_ptr<JobState>> Children;

#if SWIM_JOBS_USE_ENKITS
			std::unique_ptr<enki::TaskSet> Task;
			std::unique_ptr<enki::LambdaPinnedTask> Pinned;
			std::vector<enki::Dependency> EnkiDependencies;

			enki::ICompletable* GetCompletable()
			{
				return Task ? static_cast<enki::ICompletable*>(Task.get()) : static_cast<enki::ICompletable*>(Pinned.get());
			}

			const enki::ICompletable* GetCompletable() const
			{
				return Task ? static_cast<const enki::ICompletable*>(Task.get()) : static_cast<const enki::ICompletable*>(Pinned.get());
			}
#else
			std::function<void()> FallbackExecute;
			std::atomic<bool> FallbackComplete{ false };
#endif
		};

	}

	namespace
	{

#if SWIM_JOBS_USE_ENKITS
		enki::TaskPriority ToEnkiPriority(JobPriority priority)
		{
			switch (priority)
			{
				case JobPriority::High:
					return enki::TASK_PRIORITY_HIGH;
				case JobPriority::Low:
					return enki::TASK_PRIORITY_LOW;
				case JobPriority::Normal:
				default:
					return enki::TASK_PRIORITY_MED;
			}
		}
#endif

		void ValidateHandle(const JobHandle& job, const char* operation)
		{
			if (!job.IsValid())
			{
				throw std::invalid_argument(std::string(operation) + " requires a valid JobHandle");
			}
		}

		bool DependsTransitivelyOn(
			const std::shared_ptr<Detail::JobState>& start,
			const std::shared_ptr<Detail::JobState>& target
		)
		{
			std::vector<std::shared_ptr<Detail::JobState>> stack;
			std::unordered_set<const Detail::JobState*> visited;
			stack.push_back(start);

			while (!stack.empty())
			{
				auto current = std::move(stack.back());
				stack.pop_back();

				if (!current || !visited.insert(current.get()).second)
				{
					continue;
				}

				if (current == target)
				{
					return true;
				}

				for (const auto& parent : current->Parents)
				{
					stack.push_back(parent);
				}
			}

			return false;
		}

	}

	struct JobSystem::Impl
	{
		JobSystemDesc Desc{};
		bool Running = false;
		std::uint32_t WorkerThreads = 0;
		std::vector<std::shared_ptr<Detail::JobState>> Outstanding;
		std::mutex OutstandingMutex;
		std::atomic<std::uint32_t> NextBlockingLane{ 0 };
#if !SWIM_JOBS_USE_ENKITS
		std::atomic<std::uint32_t> FallbackExternalThreadsRegistered{ 0 };
#endif

#if SWIM_JOBS_USE_ENKITS
		enki::TaskScheduler Scheduler;

		struct BlockingLoopTask final : enki::IPinnedTask
		{
			explicit BlockingLoopTask(enki::TaskScheduler& scheduler)
				: Scheduler(&scheduler)
			{
			}

			void Execute() override
			{
				while (!Scheduler->GetIsShutdownRequested())
				{
					Scheduler->WaitForNewPinnedTasks();
					Scheduler->RunPinnedTasks();
				}
			}

			enki::TaskScheduler* Scheduler = nullptr;
		};

		std::vector<std::unique_ptr<BlockingLoopTask>> BlockingLoops;
		std::vector<std::uint32_t> BlockingThreadNumbers;
#endif

		void RequireRunning(const char* operation) const
		{
			if (!Running)
			{
				throw std::logic_error(std::string(operation) + " requires an initialized JobSystem");
			}
		}

		void TrackGraph(const std::shared_ptr<Detail::JobState>& state)
		{
			if (!state)
			{
				return;
			}

			if (!state->Tracked.exchange(true, std::memory_order_acq_rel))
			{
				std::lock_guard lock(OutstandingMutex);
				Outstanding.push_back(state);
			}

			state->Active.store(true, std::memory_order_release);
			for (const auto& childWeak : state->Children)
			{
				if (auto child = childWeak.lock())
				{
					TrackGraph(child);
				}
			}
		}

		void CollectCompleted()
		{
			std::lock_guard lock(OutstandingMutex);
			std::erase_if(Outstanding, [](const std::shared_ptr<Detail::JobState>& state)
			{
				if (!state || !state->Active.load(std::memory_order_acquire))
				{
					return false;
				}
#if SWIM_JOBS_USE_ENKITS
				return state->GetCompletable()->GetIsComplete();
#else
				return state->FallbackComplete.load(std::memory_order_acquire);
#endif
			});
		}

		std::vector<std::shared_ptr<Detail::JobState>> SnapshotOutstanding()
		{
			std::lock_guard lock(OutstandingMutex);
			return Outstanding;
		}

		void RequestCancelOutstanding()
		{
			const auto outstanding = SnapshotOutstanding();
			for (const auto& state : outstanding)
			{
				if (state)
				{
					state->CancelRequested.store(true, std::memory_order_release);
				}
			}
		}

#if SWIM_JOBS_USE_ENKITS
		std::uint32_t NextBlockingThread()
		{
			if (BlockingThreadNumbers.empty())
			{
				throw std::logic_error("JobSystem was initialized without a blocking lane");
			}

			const std::uint32_t index = NextBlockingLane.fetch_add(1, std::memory_order_relaxed)
				% static_cast<std::uint32_t>(BlockingThreadNumbers.size());
			return BlockingThreadNumbers[index];
		}
#else
		void ExecuteFallbackGraph(const std::shared_ptr<Detail::JobState>& state)
		{
			if (!state || state->FallbackComplete.load(std::memory_order_acquire))
			{
				return;
			}

			for (const auto& parent : state->Parents)
			{
				if (!parent->FallbackComplete.load(std::memory_order_acquire))
				{
					return;
				}
			}

			if (!state->CancelRequested.load(std::memory_order_acquire) && state->FallbackExecute)
			{
				state->FallbackExecute();
			}
			state->FallbackComplete.store(true, std::memory_order_release);

			for (const auto& childWeak : state->Children)
			{
				if (auto child = childWeak.lock())
				{
					ExecuteFallbackGraph(child);
				}
			}
		}
#endif
	};

	bool JobHandle::IsValid() const
	{
		return static_cast<bool>(state);
	}

	bool JobHandle::IsSubmitted() const
	{
		return state && state->Active.load(std::memory_order_acquire);
	}

	bool JobHandle::IsComplete() const
	{
		if (!state || !state->Active.load(std::memory_order_acquire))
		{
			return false;
		}
#if SWIM_JOBS_USE_ENKITS
		return state->GetCompletable()->GetIsComplete();
#else
		return state->FallbackComplete.load(std::memory_order_acquire);
#endif
	}

	bool JobHandle::IsCancellationRequested() const
	{
		return state && state->CancelRequested.load(std::memory_order_acquire);
	}

	void TaskGroup::Add(const JobHandle& job)
	{
		if (job)
		{
			jobs.push_back(job);
		}
	}

	void TaskGroup::Clear()
	{
		jobs.clear();
	}

	JobSystem::JobSystem()
		: impl(std::make_unique<Impl>())
	{
	}

	JobSystem::~JobSystem()
	{
		Shutdown(JobShutdownMode::Drain);
	}

	bool JobSystem::Initialize(const JobSystemDesc& desc)
	{
		if (impl->Running)
		{
			return true;
		}

		impl->Desc = desc;
#if SWIM_JOBS_USE_ENKITS
		const std::uint32_t hardwareThreads = std::max<std::uint32_t>(enki::GetNumHardwareThreads(), 1);
#else
		const std::uint32_t hardwareThreads = std::max<std::uint32_t>(std::thread::hardware_concurrency(), 1);
#endif
		impl->WorkerThreads = desc.WorkerThreads == 0
			? std::max<std::uint32_t>(hardwareThreads - 1, 1)
			: std::max<std::uint32_t>(desc.WorkerThreads, 1);

#if SWIM_JOBS_USE_ENKITS
		enki::TaskSchedulerConfig config = impl->Scheduler.GetConfig();
		config.numTaskThreadsToCreate = impl->WorkerThreads + desc.BlockingThreads;
		config.numExternalTaskThreads = desc.ExternalThreads;
		impl->Scheduler.Initialize(config);

		const std::uint32_t totalSchedulerThreads = impl->Scheduler.GetNumTaskThreads();

		impl->BlockingLoops.clear();
		impl->BlockingThreadNumbers.clear();
		for (std::uint32_t i = 0; i < desc.BlockingThreads; ++i)
		{
			// enkiTS thread 0 is the caller/main thread, followed by the reserved
			// external-thread registration slots, then internally-created scheduler
			// threads. Compute workers occupy the first internal range, so blocking
			// lanes are the final internally-created threads.
			const std::uint32_t threadNumber = 1 + desc.ExternalThreads + impl->WorkerThreads + i;
			if (threadNumber >= totalSchedulerThreads)
			{
				impl->Scheduler.WaitforAllAndShutdown();
				impl->BlockingLoops.clear();
				impl->BlockingThreadNumbers.clear();
				return false;
			}
			auto loop = std::make_unique<Impl::BlockingLoopTask>(impl->Scheduler);
			loop->threadNum = threadNumber;
			impl->BlockingThreadNumbers.push_back(threadNumber);
			impl->Scheduler.AddPinnedTask(loop.get());
			impl->BlockingLoops.push_back(std::move(loop));
		}
#endif

		impl->Running = true;
		return true;
	}

	void JobSystem::Shutdown(JobShutdownMode mode)
	{
		if (!impl || !impl->Running)
		{
			return;
		}

		if (mode == JobShutdownMode::CancelPending)
		{
			// enkiTS ShutdownNow intentionally leaves queued task objects in an
			// undefined state. Swim keeps its task wrappers alive and instead makes
			// cancellation cooperative, then drains the no-op wrappers before teardown.
			impl->RequestCancelOutstanding();
		}

		WaitForAll();

#if SWIM_JOBS_USE_ENKITS
		// The permanent blocking-lane pinned loops watch GetIsShutdownRequested(),
		// so WaitforAllAndShutdown is the operation which releases those lanes.
		impl->Scheduler.WaitforAllAndShutdown();
		impl->BlockingLoops.clear();
		impl->BlockingThreadNumbers.clear();
#else
		(void)mode;
#endif

		{
			std::lock_guard lock(impl->OutstandingMutex);
			impl->Outstanding.clear();
		}
		impl->Running = false;
	}

	bool JobSystem::IsRunning() const
	{
		return impl && impl->Running;
	}

	std::uint32_t JobSystem::GetWorkerThreadCount() const
	{
		return impl ? impl->WorkerThreads : 0;
	}

	std::uint32_t JobSystem::GetWorkerSlotCount() const
	{
		return impl ? impl->WorkerThreads + 1 : 1;
	}

	std::uint32_t JobSystem::GetBlockingThreadCount() const
	{
		return impl ? impl->Desc.BlockingThreads : 0;
	}

	JobHandle JobSystem::CreateJob(JobFunction function, JobPriority priority)
	{
		impl->RequireRunning("CreateJob");
		if (!function)
		{
			throw std::invalid_argument("CreateJob requires a callable");
		}

		auto state = std::make_shared<Detail::JobState>();
		state->Kind = Detail::JobKind::Task;
#if SWIM_JOBS_USE_ENKITS
		std::weak_ptr<Detail::JobState> weakState = state;
		state->Task = std::make_unique<enki::TaskSet>(1, [weakState, function = std::move(function)](enki::TaskSetPartition, std::uint32_t workerIndex)
		{
			if (auto locked = weakState.lock())
			{
				if (!locked->CancelRequested.load(std::memory_order_acquire))
				{
					Swim::Memory::ScratchScope scratch;
					function(workerIndex);
				}
			}
		});
		state->Task->m_Priority = ToEnkiPriority(priority);
#else
		state->FallbackExecute = [state, function = std::move(function)]()
		{
			if (!state->CancelRequested.load(std::memory_order_acquire))
			{
				Swim::Memory::ScratchScope scratch;
				function(0);
			}
		};
		(void)priority;
#endif
		return JobHandle(std::move(state));
	}

	JobHandle JobSystem::CreateParallelFor(
		std::size_t itemCount,
		std::size_t minItemsPerTask,
		RangeFunction function,
		JobPriority priority
	)
	{
		impl->RequireRunning("CreateParallelFor");
		if (!function)
		{
			throw std::invalid_argument("CreateParallelFor requires a callable");
		}
		if (itemCount > std::numeric_limits<std::uint32_t>::max())
		{
			throw std::overflow_error("CreateParallelFor currently supports at most UINT32_MAX items");
		}

		auto state = std::make_shared<Detail::JobState>();
		state->Kind = Detail::JobKind::Task;
		const std::uint32_t setSize = static_cast<std::uint32_t>(std::max<std::size_t>(itemCount, 1));
		const std::uint32_t minRange = static_cast<std::uint32_t>(std::clamp<std::size_t>(
			std::max<std::size_t>(minItemsPerTask, 1),
			1,
			std::numeric_limits<std::uint32_t>::max()
		));

#if SWIM_JOBS_USE_ENKITS
		std::weak_ptr<Detail::JobState> weakState = state;
		state->Task = std::make_unique<enki::TaskSet>(setSize, [weakState, itemCount, function = std::move(function)](enki::TaskSetPartition range, std::uint32_t workerIndex)
		{
			if (itemCount == 0)
			{
				return;
			}
			if (auto locked = weakState.lock())
			{
				if (!locked->CancelRequested.load(std::memory_order_acquire))
				{
					Swim::Memory::ScratchScope scratch;
					function(range.start, range.end, workerIndex);
				}
			}
		});
		state->Task->m_MinRange = minRange;
		state->Task->m_Priority = ToEnkiPriority(priority);
#else
		state->FallbackExecute = [state, itemCount, function = std::move(function)]()
		{
			if (itemCount > 0 && !state->CancelRequested.load(std::memory_order_acquire))
			{
				Swim::Memory::ScratchScope scratch;
				function(0, itemCount, 0);
			}
		};
		(void)setSize;
		(void)minRange;
		(void)priority;
#endif
		return JobHandle(std::move(state));
	}

	JobHandle JobSystem::CreateMainThreadJob(std::function<void()> function, JobPriority priority)
	{
		impl->RequireRunning("CreateMainThreadJob");
		if (!function)
		{
			throw std::invalid_argument("CreateMainThreadJob requires a callable");
		}

		auto state = std::make_shared<Detail::JobState>();
		state->Kind = Detail::JobKind::PinnedMain;
#if SWIM_JOBS_USE_ENKITS
		std::weak_ptr<Detail::JobState> weakState = state;
		state->Pinned = std::make_unique<enki::LambdaPinnedTask>(0, [weakState, function = std::move(function)]()
		{
			if (auto locked = weakState.lock())
			{
				if (!locked->CancelRequested.load(std::memory_order_acquire))
				{
					Swim::Memory::ScratchScope scratch;
					function();
				}
			}
		});
		state->Pinned->m_Priority = ToEnkiPriority(priority);
#else
		state->FallbackExecute = [state, function = std::move(function)]()
		{
			if (!state->CancelRequested.load(std::memory_order_acquire))
			{
				Swim::Memory::ScratchScope scratch;
				function();
			}
		};
		(void)priority;
#endif
		return JobHandle(std::move(state));
	}

	JobHandle JobSystem::CreateBlockingJob(std::function<void()> function, JobPriority priority)
	{
		impl->RequireRunning("CreateBlockingJob");
		if (!function)
		{
			throw std::invalid_argument("CreateBlockingJob requires a callable");
		}
		if (impl->Desc.BlockingThreads == 0)
		{
			throw std::logic_error("CreateBlockingJob requires JobSystemDesc::BlockingThreads > 0");
		}

		auto state = std::make_shared<Detail::JobState>();
		state->Kind = Detail::JobKind::PinnedBlocking;
#if SWIM_JOBS_USE_ENKITS
		const std::uint32_t threadNumber = impl->NextBlockingThread();
		std::weak_ptr<Detail::JobState> weakState = state;
		state->Pinned = std::make_unique<enki::LambdaPinnedTask>(threadNumber, [weakState, function = std::move(function)]()
		{
			if (auto locked = weakState.lock())
			{
				if (!locked->CancelRequested.load(std::memory_order_acquire))
				{
					Swim::Memory::ScratchScope scratch;
					function();
				}
			}
		});
		state->Pinned->m_Priority = ToEnkiPriority(priority);
#else
		state->FallbackExecute = [state, function = std::move(function)]()
		{
			if (!state->CancelRequested.load(std::memory_order_acquire))
			{
				Swim::Memory::ScratchScope scratch;
				function();
			}
		};
		(void)priority;
#endif
		return JobHandle(std::move(state));
	}

	void JobSystem::AddDependency(const JobHandle& job, const JobHandle& dependency)
	{
		impl->RequireRunning("AddDependency");
		ValidateHandle(job, "AddDependency");
		ValidateHandle(dependency, "AddDependency");
		if (job.state == dependency.state)
		{
			throw std::invalid_argument("A job cannot depend on itself");
		}
		if (job.IsSubmitted() || dependency.IsSubmitted())
		{
			throw std::logic_error("Job dependencies must be wired before the graph is submitted");
		}
		if (DependsTransitivelyOn(dependency.state, job.state))
		{
			throw std::logic_error("Job dependency would create a cycle");
		}

		job.state->Parents.push_back(dependency.state);
		dependency.state->Children.push_back(job.state);
#if SWIM_JOBS_USE_ENKITS
		job.state->EnkiDependencies.emplace_back();
		job.state->GetCompletable()->SetDependency(job.state->EnkiDependencies.back(), dependency.state->GetCompletable());
#endif
	}

	void JobSystem::AddDependencies(const JobHandle& job, const TaskGroup& dependencies)
	{
		for (const JobHandle& dependency : dependencies.jobs)
		{
			AddDependency(job, dependency);
		}
	}

	void JobSystem::Submit(const JobHandle& root)
	{
		impl->RequireRunning("Submit");
		ValidateHandle(root, "Submit");
		if (!root.state->Parents.empty())
		{
			throw std::logic_error("Submit accepts graph roots only; dependent jobs are released automatically");
		}
		if (root.IsSubmitted())
		{
			throw std::logic_error("Job graph root was already submitted");
		}

		impl->TrackGraph(root.state);
#if SWIM_JOBS_USE_ENKITS
		if (root.state->Task)
		{
			impl->Scheduler.AddTaskSetToPipe(root.state->Task.get());
		}
		else
		{
			impl->Scheduler.AddPinnedTask(root.state->Pinned.get());
		}
#else
		impl->ExecuteFallbackGraph(root.state);
#endif
	}

	void JobSystem::Submit(const TaskGroup& roots)
	{
		impl->RequireRunning("Submit");
		for (const JobHandle& root : roots.jobs)
		{
			ValidateHandle(root, "Submit");
			if (!root.state->Parents.empty())
			{
				throw std::logic_error("TaskGroup submission contains a non-root job");
			}
			if (root.IsSubmitted())
			{
				throw std::logic_error("TaskGroup submission contains an already-submitted root");
			}
		}

		for (const JobHandle& root : roots.jobs)
		{
			impl->TrackGraph(root.state);
		}

		for (const JobHandle& root : roots.jobs)
		{
#if SWIM_JOBS_USE_ENKITS
			if (root.state->Task)
			{
				impl->Scheduler.AddTaskSetToPipe(root.state->Task.get());
			}
			else
			{
				impl->Scheduler.AddPinnedTask(root.state->Pinned.get());
			}
#else
			impl->ExecuteFallbackGraph(root.state);
#endif
		}
	}

	JobHandle JobSystem::Schedule(JobFunction function, JobPriority priority)
	{
		JobHandle job = CreateJob(std::move(function), priority);
		Submit(job);
		return job;
	}

	JobHandle JobSystem::ScheduleMainThread(std::function<void()> function, JobPriority priority)
	{
		JobHandle job = CreateMainThreadJob(std::move(function), priority);
		Submit(job);
		return job;
	}

	JobHandle JobSystem::ScheduleBlocking(std::function<void()> function, JobPriority priority)
	{
		JobHandle job = CreateBlockingJob(std::move(function), priority);
		Submit(job);
		return job;
	}

	void JobSystem::Wait(const JobHandle& job)
	{
		impl->RequireRunning("Wait");
		ValidateHandle(job, "Wait");
		if (!job.IsSubmitted())
		{
			throw std::logic_error("Cannot wait for a job graph that has not been submitted");
		}
#if SWIM_JOBS_USE_ENKITS
		impl->Scheduler.WaitforTask(job.state->GetCompletable());
#else
		if (!job.IsComplete())
		{
			throw std::logic_error("Offline JobSystem fallback cannot make progress on an incomplete graph");
		}
#endif
		impl->CollectCompleted();
	}

	void JobSystem::Wait(const TaskGroup& group)
	{
		for (const JobHandle& job : group.jobs)
		{
			Wait(job);
		}
	}

	void JobSystem::WaitForAll()
	{
		impl->RequireRunning("WaitForAll");

		// Do not call enkiTS WaitforAll() here: Swim deliberately keeps pinned
		// blocking-lane loops alive for the lifetime of the scheduler. Instead wait
		// only for submitted Swim job wrappers captured by this snapshot.
		const auto outstanding = impl->SnapshotOutstanding();
		for (const auto& state : outstanding)
		{
			if (!state || !state->Active.load(std::memory_order_acquire))
			{
				continue;
			}
#if SWIM_JOBS_USE_ENKITS
			impl->Scheduler.WaitforTask(state->GetCompletable());
#else
			if (!state->FallbackComplete.load(std::memory_order_acquire))
			{
				throw std::logic_error("Offline JobSystem fallback cannot make progress on an incomplete graph");
			}
#endif
		}

		impl->CollectCompleted();
	}

	void JobSystem::ParallelFor(
		std::size_t itemCount,
		std::size_t minItemsPerTask,
		RangeFunction function,
		JobPriority priority
	)
	{
		if (itemCount == 0)
		{
			return;
		}
		JobHandle job = CreateParallelFor(itemCount, minItemsPerTask, std::move(function), priority);
		Submit(job);
		Wait(job);
	}

	void JobSystem::RunMainThreadJobs()
	{
		impl->RequireRunning("RunMainThreadJobs");
#if SWIM_JOBS_USE_ENKITS
		impl->Scheduler.RunPinnedTasks();
#endif
		impl->CollectCompleted();
	}

	void JobSystem::Cancel(const JobHandle& job)
	{
		ValidateHandle(job, "Cancel");
		job.state->CancelRequested.store(true, std::memory_order_release);
	}

	void JobSystem::Cancel(const TaskGroup& group)
	{
		for (const JobHandle& job : group.jobs)
		{
			Cancel(job);
		}
	}

	bool JobSystem::RegisterCurrentExternalThread()
	{
		impl->RequireRunning("RegisterCurrentExternalThread");
#if SWIM_JOBS_USE_ENKITS
		return impl->Scheduler.RegisterExternalTaskThread();
#else
		std::uint32_t current = impl->FallbackExternalThreadsRegistered.load(std::memory_order_acquire);
		while (current < impl->Desc.ExternalThreads)
		{
			if (impl->FallbackExternalThreadsRegistered.compare_exchange_weak(
				current,
				current + 1,
				std::memory_order_acq_rel,
				std::memory_order_acquire))
			{
				return true;
			}
		}
		return false;
#endif
	}

	void JobSystem::UnregisterCurrentExternalThread()
	{
		impl->RequireRunning("UnregisterCurrentExternalThread");
#if SWIM_JOBS_USE_ENKITS
		impl->Scheduler.DeRegisterExternalTaskThread();
#else
		std::uint32_t current = impl->FallbackExternalThreadsRegistered.load(std::memory_order_acquire);
		while (current > 0)
		{
			if (impl->FallbackExternalThreadsRegistered.compare_exchange_weak(
				current,
				current - 1,
				std::memory_order_acq_rel,
				std::memory_order_acquire))
			{
				break;
			}
		}
#endif
	}

}
