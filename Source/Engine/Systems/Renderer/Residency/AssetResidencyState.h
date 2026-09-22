#pragma once
#include <cstdint>

namespace Swim::Render
{
	// The Phase 11 "no hidden upload" state machine for one requested asset:
	// Unloaded -> Queued -> Reading -> Decoding -> WaitingForGpuUpload ->
	// Uploading -> Resident, or Failed with an Assets::AssetError. Decoding covers
	// hash validation and payload decode; WaitingForGpuUpload means the CPU asset
	// is published and waits for the per-update upload budget.
	enum class AssetResidencyState : std::uint8_t
	{
		Unloaded,
		Queued,
		Reading,
		Decoding,
		WaitingForGpuUpload,
		Uploading,
		Resident,
		Failed
	};
} // namespace Swim::Render
