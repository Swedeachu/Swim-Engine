#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace Swim::Jobs
{

	enum class JobPriority : std::uint8_t
	{
		High,
		Normal,
		Low
	};

	enum class JobShutdownMode : std::uint8_t
	{
		Drain,
		CancelPending
	};

	struct JobSystemDesc
	{
		// Zero selects hardware_threads - 1 compute workers. Thread 0 remains the
		// caller/main thread and can participate in waits and ParallelFor work.
		std::uint32_t WorkerThreads = 0;

		// Blocking lanes are additional enkiTS threads which run pinned-task loops.
		// They are reserved for filesystem/network work so blocking syscalls cannot
		// consume compute-worker capacity.
		std::uint32_t BlockingThreads = 1;
		std::uint32_t ExternalThreads = 0;
	};

	namespace Detail
	{
		struct JobState;
	}

	class JobHandle
	{
	public:

		JobHandle() = default;

		bool IsValid() const;
		bool IsSubmitted() const;
		bool IsComplete() const;
		bool IsCancellationRequested() const;

		explicit operator bool() const { return IsValid(); }

	private:

		explicit JobHandle(std::shared_ptr<Detail::JobState> state)
			: state(std::move(state))
		{
		}

		std::shared_ptr<Detail::JobState> state;

		friend class JobSystem;
		friend class TaskGroup;
	};

	class TaskGroup
	{
	public:

		void Add(const JobHandle& job);
		void Clear();
		bool Empty() const { return jobs.empty(); }
		std::size_t Size() const { return jobs.size(); }

	private:

		std::vector<JobHandle> jobs;
		friend class JobSystem;
	};

	class JobSystem
	{
	public:

		using JobFunction = std::function<void(std::uint32_t workerIndex)>;
		using RangeFunction = std::function<void(std::size_t begin, std::size_t end, std::uint32_t workerIndex)>;

		JobSystem();
		~JobSystem();

		JobSystem(const JobSystem&) = delete;
		JobSystem& operator=(const JobSystem&) = delete;

		bool Initialize(const JobSystemDesc& desc = {});
		void Shutdown(JobShutdownMode mode = JobShutdownMode::Drain);

		bool IsRunning() const;
		std::uint32_t GetWorkerThreadCount() const;
		std::uint32_t GetWorkerSlotCount() const;
		std::uint32_t GetBlockingThreadCount() const;

		JobHandle CreateJob(JobFunction function, JobPriority priority = JobPriority::Normal);
		JobHandle CreateParallelFor(
			std::size_t itemCount,
			std::size_t minItemsPerTask,
			RangeFunction function,
			JobPriority priority = JobPriority::Normal
		);
		JobHandle CreateMainThreadJob(std::function<void()> function, JobPriority priority = JobPriority::Normal);
		JobHandle CreateBlockingJob(std::function<void()> function, JobPriority priority = JobPriority::Normal);

		// Dependencies must be wired before any root in the graph is submitted.
		// Submit only roots (jobs with no dependencies); enkiTS releases dependent
		// jobs automatically when their prerequisite tasks complete.
		void AddDependency(const JobHandle& job, const JobHandle& dependency);
		void AddDependencies(const JobHandle& job, const TaskGroup& dependencies);
		void Submit(const JobHandle& root);
		void Submit(const TaskGroup& roots);

		JobHandle Schedule(JobFunction function, JobPriority priority = JobPriority::Normal);
		JobHandle ScheduleMainThread(std::function<void()> function, JobPriority priority = JobPriority::Normal);
		JobHandle ScheduleBlocking(std::function<void()> function, JobPriority priority = JobPriority::Normal);

		void Wait(const JobHandle& job);
		void Wait(const TaskGroup& group);
		void WaitForAll();

		void ParallelFor(
			std::size_t itemCount,
			std::size_t minItemsPerTask,
			RangeFunction function,
			JobPriority priority = JobPriority::Normal
		);

		void RunMainThreadJobs();
		void Cancel(const JobHandle& job);
		void Cancel(const TaskGroup& group);

		bool RegisterCurrentExternalThread();
		void UnregisterCurrentExternalThread();

	private:

		struct Impl;
		std::unique_ptr<Impl> impl;
	};

}
