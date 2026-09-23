#pragma once
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"
#include "Engine/Systems/Renderer/Visibility/VisibilityBinLayout.h"

#include <cstdint>

namespace Swim::Render
{
	// How a view issues GpuVisibility's indirect commands.
	enum class VisibilityDrawPath : std::uint8_t
	{
		// DrawIndexedIndirectCount: the GPU count limits each bin (the fast path).
		IndirectCount,
		// DrawIndexedIndirect over each bin's whole capacity. Requires
		// VisibilityFrameDesc::ZeroUnusedCommands so unused slots draw nothing.
		ZeroFilledIndirect,
	};

	// The fast path whenever the device supports it; the fallback only when it does not.
	constexpr VisibilityDrawPath SelectVisibilityDrawPath(const Rhi::GraphicsCapabilities& capabilities)
	{
		return capabilities.IndirectCount ? VisibilityDrawPath::IndirectCount : VisibilityDrawPath::ZeroFilledIndirect;
	}

	// True when frames drawn with `path` must set VisibilityFrameDesc::ZeroUnusedCommands.
	constexpr bool NeedsZeroedCommands(VisibilityDrawPath path)
	{
		return path == VisibilityDrawPath::ZeroFilledIndirect;
	}

	// Issues one bin inside a rendering pass whose pipeline, descriptors and index
	// buffer (the bin's index page) are already bound. `commands` and `counts` are
	// the phase's VisibilityGraphResources buffers declared as IndirectArgument reads.
	void DrawVisibilityBin(Rhi::CommandList& list, Rhi::Buffer& commands, Rhi::Buffer& counts, const VisibilityBinLayout& bins,
		std::uint32_t bin, VisibilityDrawPath path);
} // namespace Swim::Render
