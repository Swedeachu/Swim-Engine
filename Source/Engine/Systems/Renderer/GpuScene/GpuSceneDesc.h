#pragma once
#include <cstdint>
#include <string>

namespace Swim::Render
{
	struct GpuSceneDesc
	{
		// Live plus retiring objects. Instance and transform buffers are allocated
		// for this many rows up front (64 + 96 bytes per object).
		std::uint32_t MaxObjects = 16384;
		std::string DebugName = "GPU scene";
	};
} // namespace Swim::Render
