#pragma once
#include <cstdint>

namespace Swim::Render
{
	// Initial staging reservations. Zero creates arenas lazily. The executor grows
	// an arena only between submissions, after waiting for its previous execution,
	// to the smallest power of two that holds the current graph's staged buffers.
	// Arenas never grow while bytes are in flight and never spill per allocation.
	struct RenderGraphExecutorDesc
	{
		std::uint64_t UploadCapacity = 0;
		std::uint64_t ReadbackCapacity = 0;
	};
} // namespace Swim::Render
