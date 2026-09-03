#pragma once

#include "Engine/Jobs/JobSystem.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

// Legacy renderer tuning constants retained while call sites move to the
// engine-wide JobSystem. There is deliberately no renderer-owned worker pool.

namespace Engine
{

	struct RenderCpuJobConfig
	{
		static constexpr bool Enabled = true;
		static constexpr std::uint32_t ChunksPerWorker = 2;
		static constexpr std::size_t DefaultMinItemsPerChunk = 128;
		static constexpr std::size_t MinParallelItemCount = 512;
	};

	inline std::size_t GetRenderParallelWorkerSlots(const Swim::Jobs::JobSystem& jobs)
	{
		if constexpr (!RenderCpuJobConfig::Enabled)
		{
			return 1;
		}

		return std::max<std::size_t>(jobs.GetWorkerSlotCount(), 1);
	}

	template<typename Func>
	inline void ParallelForRender(
		Swim::Jobs::JobSystem& jobs,
		std::size_t itemCount,
		std::size_t minItemsPerChunk,
		Func&& func
	)
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

		const std::size_t workerSlots = GetRenderParallelWorkerSlots(jobs);
		const std::size_t minChunk = std::max<std::size_t>(minItemsPerChunk, 1);
		if (workerSlots <= 1 || itemCount < std::max(minChunk, RenderCpuJobConfig::MinParallelItemCount))
		{
			func(0, itemCount, 0);
			return;
		}

		const std::size_t targetChunks = std::max<std::size_t>(
			workerSlots * static_cast<std::size_t>(RenderCpuJobConfig::ChunksPerWorker),
			1
		);
		const std::size_t computedChunkSize = (itemCount + targetChunks - 1) / targetChunks;
		const std::size_t chunkSize = std::max(minChunk, computedChunkSize);

		jobs.ParallelFor(
			itemCount,
			chunkSize,
			[function = std::forward<Func>(func)](std::size_t begin, std::size_t end, std::uint32_t workerIndex) mutable
			{
				function(begin, end, workerIndex);
			}
		);
	}

}
