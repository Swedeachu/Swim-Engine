#pragma once
#include <cstdint>

namespace Swim::Render
{
	struct GpuSamplerCacheStats
	{
		std::uint32_t Live = 0;
		std::uint32_t Retiring = 0;
		std::uint32_t References = 0; // Outstanding Acquire calls across live samplers.
		std::uint64_t Created = 0;	  // RHI samplers created.
		std::uint64_t Reused = 0;	  // Acquire calls satisfied by an existing sampler.
	};
} // namespace Swim::Render
