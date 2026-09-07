#pragma once

#include "Engine/Systems/Renderer/RHI/RhiTypes.h"

#include <vector>

namespace Swim::Rhi
{

	enum class SwapchainColorMode : std::uint8_t
	{
		Sdr,
		PreferHdr, // Falls back to SDR only when no supported HDR pair is available.
		RequireHdr, // Never silently falls back to SDR.
	};

	enum class SwapchainColorSpace : std::uint8_t
	{
		Undefined,
		SrgbNonlinear,
		Hdr10St2084, // BT.2020 primaries, PQ-encoded shader output.
		ExtendedSrgbLinear, // Extended-range linear sRGB (scRGB).
	};

	struct SwapchainSurfaceFormat
	{
		Format PixelFormat = Format::Undefined;
		SwapchainColorSpace ColorSpace = SwapchainColorSpace::Undefined;

		bool IsHdr() const
		{
			return ColorSpace == SwapchainColorSpace::Hdr10St2084 ||
				ColorSpace == SwapchainColorSpace::ExtendedSrgbLinear;
		}

		bool operator==(const SwapchainSurfaceFormat&) const = default;
	};

	struct SwapchainSupport
	{
		bool PresentationSupported = false;
		// Only format/color-space pairs implemented by this backend. This is a
		// window-specific snapshot, not a promise about monitor brightness or
		// later creation. Query again after display/OS configuration changes.
		std::vector<SwapchainSurfaceFormat> Formats;

		bool SupportsHdr() const
		{
			for (const auto& format : Formats)
			{
				if (PresentationSupported && format.IsHdr())
				{
					return true;
				}
			}
			return false;
		}
	};

} // namespace Swim::Rhi
