#pragma once
#include <cstddef>
#include <cstdint>

namespace Swim::Render
{
	// GPU records of the UI renderer (Shaders/Slang/Ui/UiRecords.slang).

	// GpuUiQuad::Kind.
	inline constexpr std::uint32_t UiQuadSolid = 0; // Flat or rounded/bordered SDF rectangle.
	inline constexpr std::uint32_t UiQuadGlyph = 1; // MSDF glyph from an atlas page.
	inline constexpr std::uint32_t UiQuadImage = 2; // Premultiplied texture times the tint.

	// One UI paint quad in framebuffer pixels, drawn as one instance of six vertices.
	// The vertex stage clips Rect to Clip (exact for these axis-aligned rectangles, so no
	// fragment is discarded) and remaps the UV to the clipped part.
	struct GpuUiQuad
	{
		float Rect[4] = {};		   // x0, y0, x1, y1 (unclipped).
		float Clip[4] = {};		   // x0, y0, x1, y1.
		float Uv[4] = {};		   // u0, v0 at (x0, y0); u1, v1 at (x1, y1).
		float Color[4] = {};	   // Premultiplied linear RGBA (the tint for images).
		float BorderColor[4] = {}; // Premultiplied linear RGBA.
		float Radius = 0.0f;	   // Corner radius in pixels (solid).
		float Border = 0.0f;	   // Inner border width in pixels (solid).
		float PixelRange = 0.0f;   // Glyph: screen pixels per unit of normalized distance (>= 1).
		std::uint32_t Kind = UiQuadSolid;
		std::uint32_t Texture = 0; // Bindless texture index (glyph page or image).
		std::uint32_t Sampler = 0; // Bindless sampler index.
		std::uint32_t Reserved[2] = {};
	};

	static_assert(sizeof(GpuUiQuad) == 112);
	static_assert(offsetof(GpuUiQuad, Radius) == 80 && offsetof(GpuUiQuad, Kind) == 92 && offsetof(GpuUiQuad, Sampler) == 100);

	// Push constants of SwimUiQuad (vertex and fragment stages).
	struct GpuUiDrawConstants
	{
		float TargetSize[2] = {};	// Framebuffer pixels.
		std::uint32_t Encoding = 0; // UiOutputEncoding.
		float WhiteScale = 1.0f;	// Linear/scRGB: multiplier; HDR10: nits of UI white.
	};

	static_assert(sizeof(GpuUiDrawConstants) == 16);
} // namespace Swim::Render
