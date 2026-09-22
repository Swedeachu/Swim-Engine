#pragma once
#include <cstdint>

namespace Swim::Render
{
	// Explicit GPU upload state of a persistent renderer resource, separate from
	// CPU asset validity: PendingUpload (bytes staged on the CPU) -> Recorded
	// (upload passes added to a graph, awaiting commit/abort) -> Uploading
	// (submitted, waiting for its completion value) -> Resident.
	enum class GpuUploadState : std::uint8_t
	{
		Invalid,
		PendingUpload,
		Recorded,
		Uploading,
		Resident
	};
} // namespace Swim::Render
