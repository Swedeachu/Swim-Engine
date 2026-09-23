#pragma once
#include <cstdint>
#include <string>

namespace Swim::Render
{
	struct GpuLightBufferDesc
	{
		std::uint32_t MaxDirectionalLights = 4;
		std::uint32_t MaxLocalLights = 4096; // Point + spot.
		std::string DebugName = "GPU lights";
	};

	struct GpuLightBufferStats
	{
		std::uint32_t DirectionalCapacity = 0;
		std::uint32_t LocalCapacity = 0;
		std::uint32_t DirectionalLights = 0;
		std::uint32_t LocalLights = 0;
		std::uint32_t DirtyRows = 0;	  // Waiting for the next Import.
		std::uint32_t LastUploadRows = 0; // Rows recorded by the last Import.
		std::uint32_t LastUploadRuns = 0;
		std::uint64_t LastUploadBytes = 0; // Rows plus the header, when it changed.
		bool LastUploadHeader = false;
	};
} // namespace Swim::Render
