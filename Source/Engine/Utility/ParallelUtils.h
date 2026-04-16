#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>
#include <thread>
#include <vector>

// A header file only thread pool implementation intended for renderer based work on the CPU side, such as culling sections of the world from the child regions of the BVH.

namespace Engine
{

	struct RenderCpuJobConfig
	{
		static constexpr bool Enabled = true; // if false, the engine does not use multi threading for anything (well except for audio and PhysX)
		static constexpr uint32_t ReserveHardwareThreads = 1;
		static constexpr uint32_t MaxWorkerThreads = 16;
		static constexpr uint32_t ChunksPerWorker = 2;
		static constexpr size_t DefaultMinItemsPerChunk = 128;
	};

	class RenderThreadPool
	{

	public:

		using RangeExecuteFn = void(*)(void* context, size_t begin, size_t end, uint32_t workerIndex);

		static RenderThreadPool& Get()
		{
			static RenderThreadPool instance;
			return instance;
		}

		RenderThreadPool(const RenderThreadPool&) = delete;
		RenderThreadPool& operator=(const RenderThreadPool&) = delete;

		size_t GetWorkerThreadCount() const
		{
			return workers.size();
		}

		size_t GetWorkerSlotCount() const
		{
			return workers.size() + 1;
		}

		template<typename Func>
		void ParallelFor(size_t itemCount, size_t minItemsPerChunk, Func&& func)
		{
			if (itemCount == 0)
			{
				return;
			}

			if constexpr (!RenderCpuJobConfig::Enabled)
			{
				func(0, itemCount, 0);
				return;
			}

			if (tIsRenderWorker)
			{
				func(0, itemCount, tWorkerSlotIndex);
				return;
			}

			const size_t workerThreadCount = workers.size();
			const size_t minChunk = std::max<size_t>(minItemsPerChunk, 1);
			const size_t possibleChunks = (itemCount + minChunk - 1) / minChunk;
			if (workerThreadCount == 0 || possibleChunks <= 1)
			{
				func(0, itemCount, 0);
				return;
			}

			const uint32_t activeWorkerThreads = static_cast<uint32_t>(std::min<size_t>(workerThreadCount, possibleChunks - 1));
			if (activeWorkerThreads == 0)
			{
				func(0, itemCount, 0);
				return;
			}

			const size_t totalParticipants = static_cast<size_t>(activeWorkerThreads) + 1;
			const size_t targetChunks = std::max<size_t>(totalParticipants * static_cast<size_t>(RenderCpuJobConfig::ChunksPerWorker), 1);
			const size_t computedChunkSize = (itemCount + targetChunks - 1) / targetChunks;
			const size_t chunkSize = std::max(minChunk, computedChunkSize);
			const uint32_t chunkCount = static_cast<uint32_t>((itemCount + chunkSize - 1) / chunkSize);

			if (chunkCount <= 1)
			{
				func(0, itemCount, 0);
				return;
			}

			auto* funcPtr = std::addressof(func);
			const RangeExecuteFn execute = [](void* context, size_t begin, size_t end, uint32_t workerIndex)
			{
				(*static_cast<std::remove_reference_t<Func>*>(context))(begin, end, workerIndex);
			};

			DispatchRange(funcPtr, execute, itemCount, chunkSize, chunkCount, activeWorkerThreads);
		}

	private:

		struct DispatchState
		{
			void* context = nullptr;
			RangeExecuteFn execute = nullptr;
			size_t itemCount = 0;
			size_t chunkSize = 1;
			uint32_t chunkCount = 0;
			uint32_t activeWorkerThreads = 0;
			uint32_t completedWorkers = 0;
			uint64_t generation = 0;
			bool stop = false;
			bool active = false;
		};

		RenderThreadPool()
		{
			if constexpr (!RenderCpuJobConfig::Enabled)
			{
				return;
			}

			uint32_t hardwareThreads = std::thread::hardware_concurrency();
			if (hardwareThreads == 0)
			{
				hardwareThreads = 1;
			}

			uint32_t desiredWorkers = 0;
			if (hardwareThreads > RenderCpuJobConfig::ReserveHardwareThreads)
			{
				desiredWorkers = hardwareThreads - RenderCpuJobConfig::ReserveHardwareThreads;
			}
			else if (hardwareThreads > 1)
			{
				desiredWorkers = hardwareThreads - 1;
			}

			desiredWorkers = std::min<uint32_t>(desiredWorkers, RenderCpuJobConfig::MaxWorkerThreads);
			std::cout << "Multi-threaded Vulkan Renderer constructed with " << std::to_string(desiredWorkers) << " worker threads" << std::endl;
			workers.reserve(desiredWorkers);

			for (uint32_t workerIndex = 0; workerIndex < desiredWorkers; ++workerIndex)
			{
				workers.emplace_back(&RenderThreadPool::WorkerMain, this, workerIndex + 1);
			}
		}

		~RenderThreadPool()
		{
			if constexpr (!RenderCpuJobConfig::Enabled)
			{
				return;
			}

			{
				std::lock_guard<std::mutex> lock(dispatchMutex);
				dispatch.stop = true;
				++dispatch.generation;
			}
			dispatchWakeCv.notify_all();

			for (std::thread& worker : workers)
			{
				if (worker.joinable())
				{
					worker.join();
				}
			}
		}

		void DispatchRange(void* context, RangeExecuteFn execute, size_t itemCount, size_t chunkSize, uint32_t chunkCount, uint32_t activeWorkerThreads)
		{
			if (activeWorkerThreads == 0 || execute == nullptr || itemCount == 0 || chunkCount == 0)
			{
				if (execute != nullptr && itemCount > 0)
				{
					execute(context, 0, itemCount, 0);
				}
				return;
			}

			{
				std::unique_lock<std::mutex> lock(dispatchMutex);
				dispatch.context = context;
				dispatch.execute = execute;
				dispatch.itemCount = itemCount;
				dispatch.chunkSize = chunkSize;
				dispatch.chunkCount = chunkCount;
				dispatch.activeWorkerThreads = activeWorkerThreads;
				dispatch.completedWorkers = 0;
				dispatch.active = true;
				++dispatch.generation;
			}

			dispatchWakeCv.notify_all();
			ExecuteAssignedWork(context, execute, itemCount, chunkSize, chunkCount, activeWorkerThreads, 0);

			{
				std::unique_lock<std::mutex> lock(dispatchMutex);
				dispatchDoneCv.wait(lock, [&]()
				{
					return dispatch.completedWorkers >= dispatch.activeWorkerThreads;
				});
				dispatch.active = false;
				dispatch.context = nullptr;
				dispatch.execute = nullptr;
				dispatch.itemCount = 0;
				dispatch.chunkSize = 1;
				dispatch.chunkCount = 0;
				dispatch.activeWorkerThreads = 0;
				dispatch.completedWorkers = 0;
			}
		}

		void ExecuteAssignedWork(void* context, RangeExecuteFn execute, size_t itemCount, size_t chunkSize, uint32_t chunkCount, uint32_t activeWorkerThreads, uint32_t workerIndex)
		{
			const uint32_t participantCount = activeWorkerThreads + 1;
			for (uint32_t chunkIndex = workerIndex; chunkIndex < chunkCount; chunkIndex += participantCount)
			{
				const size_t begin = static_cast<size_t>(chunkIndex) * chunkSize;
				if (begin >= itemCount)
				{
					break;
				}

				const size_t end = std::min(begin + chunkSize, itemCount);
				execute(context, begin, end, workerIndex);
			}
		}

		void WorkerMain(uint32_t workerIndex)
		{
			tIsRenderWorker = true;
			tWorkerSlotIndex = workerIndex;

			uint64_t seenGeneration = 0;

			while (true)
			{
				void* context = nullptr;
				RangeExecuteFn execute = nullptr;
				size_t itemCount = 0;
				size_t chunkSize = 1;
				uint32_t chunkCount = 0;
				uint32_t activeWorkerThreads = 0;
				bool shouldRun = false;

				{
					std::unique_lock<std::mutex> lock(dispatchMutex);
					dispatchWakeCv.wait(lock, [&]()
					{
						return dispatch.stop || dispatch.generation != seenGeneration;
					});

					if (dispatch.stop)
					{
						return;
					}

					seenGeneration = dispatch.generation;
					if (dispatch.active && workerIndex <= dispatch.activeWorkerThreads)
					{
						context = dispatch.context;
						execute = dispatch.execute;
						itemCount = dispatch.itemCount;
						chunkSize = dispatch.chunkSize;
						chunkCount = dispatch.chunkCount;
						activeWorkerThreads = dispatch.activeWorkerThreads;
						shouldRun = true;
					}
				}

				if (!shouldRun)
				{
					continue;
				}

				ExecuteAssignedWork(context, execute, itemCount, chunkSize, chunkCount, activeWorkerThreads, workerIndex);

				{
					std::lock_guard<std::mutex> lock(dispatchMutex);
					++dispatch.completedWorkers;
				}
				dispatchDoneCv.notify_one();
			}
		}

		std::vector<std::thread> workers;
		mutable std::mutex dispatchMutex;
		std::condition_variable dispatchWakeCv;
		std::condition_variable dispatchDoneCv;
		DispatchState dispatch;

		inline static thread_local bool tIsRenderWorker = false;
		inline static thread_local uint32_t tWorkerSlotIndex = 0;
	};

	inline size_t GetRenderParallelWorkerSlots()
	{
		if constexpr (!RenderCpuJobConfig::Enabled)
		{
			return 1;
		}

		return RenderThreadPool::Get().GetWorkerSlotCount();
	}

	template<typename Func>
	inline void ParallelForRender(size_t itemCount, size_t minItemsPerChunk, Func&& func)
	{
		if constexpr (!RenderCpuJobConfig::Enabled)
		{
			if (itemCount > 0)
			{
				func(0, itemCount, 0);
			}
			return;
		}

		RenderThreadPool::Get().ParallelFor(itemCount, minItemsPerChunk, std::forward<Func>(func));
	}

}
