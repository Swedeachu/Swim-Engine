#pragma once
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

namespace Swim::Render
{
	// The physical bytes backing a graph buffer during recording. Ordinary
	// buffers cover their whole allocation; staged upload/readback buffers are
	// suballocations, so commands must add Offset to every buffer offset.
	struct GraphBufferRange
	{
		Rhi::Buffer* Buffer = nullptr;
		std::uint64_t Offset = 0;
		std::uint64_t Size = 0;
	};
} // namespace Swim::Render
