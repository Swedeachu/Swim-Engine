#pragma once
#include <cstdint>

namespace Swim::Render
{
	// Explicit GPU residency of a GeometryHeap mesh, separate from asset validity:
	// PendingUpload (bytes staged on the CPU) -> Recorded (upload passes added to a
	// graph, awaiting CommitUploads/AbortUploads) -> Uploading (submitted, waiting
	// for its completion value) -> Resident.
	enum class GeometryResidency : std::uint8_t
	{
		Invalid,
		PendingUpload,
		Recorded,
		Uploading,
		Resident
	};
} // namespace Swim::Render
