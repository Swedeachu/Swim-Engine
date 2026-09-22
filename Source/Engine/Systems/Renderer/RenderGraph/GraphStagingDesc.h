#pragma once
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"
#include <functional>

namespace Swim::Render
{
	// Writes the complete upload payload into mapped, write-only staging bytes.
	// Invoked by RenderGraphExecutor::Execute before recording, once per execution
	// of a compiled graph whose passes read the upload. Do not read the span.
	using GraphUploadWriter = std::function<void(std::span<std::byte>)>;

	// A host-written, GPU-read-only buffer suballocated from the executor's upload
	// arena. It is initialized at graph start (HostWrite) and can be read in any
	// state its usage permits. Usage is a subset of TransferSource, Vertex, Index,
	// Uniform, Storage and Indirect; alignment is a power of two applied to the
	// GPU offset (the executor additionally applies uniform/storage limits).
	struct GraphUploadDesc
	{
		std::uint64_t Size = 0;
		Rhi::BufferUsage Usage = Rhi::BufferUsage::TransferSource;
		std::uint64_t Alignment = 4;
		std::string_view DebugName;
	};

	// A GPU-written, host-read buffer suballocated from the executor's readback
	// arena. It is written in CopyDestination by one initializing pass, is always
	// exported to HostRead, and is read through RenderGraphExecutor::TryReadback.
	struct GraphReadbackDesc
	{
		std::uint64_t Size = 0;
		std::uint64_t Alignment = 4;
		std::string_view DebugName;
	};
} // namespace Swim::Render
