#pragma once
#include <cstdint>

namespace Swim::Render
{
	// How UI colors (premultiplied linear Rec.709, 1.0 = UI white) reach the target.
	// UI is composited after tone mapping, never through it.
	enum class UiOutputEncoding : std::uint32_t
	{
		// The target holds sRGB-encoded values in a UNORM format (the post stack's SDR
		// output, a BGRA8Unorm swapchain). Each quad is encoded to sRGB, then blended in
		// encoded space, the conventional UI compositing space (browsers, toolkits).
		Srgb = 0,
		// The target holds linear values: *Srgb formats (the hardware encodes after a
		// linear blend) or float intermediates. Colors are scaled by WhiteScale.
		Linear = 1,
		// HDR10: BT.2020 primaries, SMPTE ST 2084 (PQ), as the post stack's HDR10 output.
		// UI white is PaperWhiteNits; quads blend in PQ space.
		Hdr10 = 2,
		// scRGB: linear BT.709 with 1.0 = 80 nits; UI white is PaperWhiteNits.
		ScRgb = 3,
	};

	struct UiCompositionSettings
	{
		UiOutputEncoding Encoding = UiOutputEncoding::Srgb;
		// HDR10/scRGB: the luminance of UI white. Match the post stack's paper white so UI
		// and SDR-referred scene white agree. (0, 10000].
		float PaperWhiteNits = 200.0f;
		// Linear: multiplier of UI colors (1 for an SDR-referred linear target).
		float LinearScale = 1.0f;
	};

	// Throws std::invalid_argument for an unknown encoding or an out-of-range value.
	void ValidateUiCompositionSettings(const UiCompositionSettings& settings);
} // namespace Swim::Render
