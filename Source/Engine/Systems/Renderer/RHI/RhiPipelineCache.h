#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Swim::Rhi
{

	// Limit includes the backend's persistence envelope. Cache data is disposable
	// acceleration data, not a shader asset or a portable pipeline description.
	inline constexpr std::size_t MaxPipelineCacheDataBytes = 64u * 1024u * 1024u;

	enum class PipelineCacheLoadStatus : std::uint8_t
	{
		Unsupported,
		Empty,
		Loaded,
		AlreadyInitialized,
		InvalidData,
		Incompatible,
		Failed,
	};

	enum class PipelineCacheDataStatus : std::uint8_t
	{
		Unsupported,
		Empty,
		Ready,
		TooLarge,
		Incomplete,
		Failed,
	};

	struct PipelineCacheData
	{
		PipelineCacheDataStatus Status = PipelineCacheDataStatus::Unsupported;
		// Owned, versioned backend data. Persist exactly as returned and load
		// only trusted local cache files; integrity checking is not authentication.
		std::vector<std::byte> Bytes;
	};

} // namespace Swim::Rhi
