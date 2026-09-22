#pragma once
#include <cstdint>
#include <string>

namespace Swim::Render
{
	struct GpuSamplerCacheDesc
	{
		// Distinct samplers alive or retiring at once. Keep well below the device's
		// sampler allocation limit (commonly 4000).
		std::uint32_t MaxSamplers = 256;
		std::string DebugName = "GPU samplers";
	};
} // namespace Swim::Render
