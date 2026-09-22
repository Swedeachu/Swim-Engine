#pragma once
#include <cstdint>

namespace Swim::Render
{
	struct AssetResidencyStats
	{
		std::uint32_t Queued = 0;
		std::uint32_t Reading = 0;
		std::uint32_t Decoding = 0;
		std::uint32_t WaitingForGpuUpload = 0;
		std::uint32_t Uploading = 0;
		std::uint32_t Resident = 0;
		std::uint32_t Failed = 0;
		std::uint32_t AbandonedDecodes = 0; // Released while a decode job was running.
		std::uint32_t BindlessTextures = 0; // Resident textures with a bindless element.
		std::uint32_t BindlessPending = 0;	// Resident textures waiting for a free element.
		std::uint64_t BytesRead = 0;		// Total successfully read .sasset bytes.
		std::uint64_t BytesStagedLastUpdate = 0;
		std::uint64_t BytesStaged = 0; // Total bytes handed to GPU residency.
	};
} // namespace Swim::Render
