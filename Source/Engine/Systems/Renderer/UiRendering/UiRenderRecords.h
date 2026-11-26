#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace Swim::Render
{

	// GPU records of the UI renderer (Shaders/Slang/Ui/UiRecords.slang).

	// GpuUiQuad::Kind.
	inline constexpr std::uint32_t UiQuadSolid = 0; // Flat or rounded/bordered SDF rectangle.
	inline constexpr std::uint32_t UiQuadGlyph = 1; // MSDF glyph from an atlas page.
	inline constexpr std::uint32_t UiQuadImage = 2; // Premultiplied texture times the tint.
	// Bitmap-font glyph from a colour atlas page: straight-alpha sRGB texels (nearest
	// sampler), decoded and premultiplied, times the tint.
	inline constexpr std::uint32_t UiQuadColorGlyph = 3;
	// An image with a glint over its opaque texels: BorderColor is the glint colour (rgb) and
	// strength (a), PixelRange the mode (1 procedural, 2 texture), UnitRange the glint
	// texture's bindless index; the time and repeating sampler are in GpuUiDrawConstants::Glint.
	inline constexpr std::uint32_t UiQuadImageGlint = 4;
	// A solid whose colour fades from Color to Uv (premultiplied RGBA) across Rect: left to
	// right, or top to bottom when PixelRange is 1. Corners and border as for solids.
	inline constexpr std::uint32_t UiQuadGradient = 5;

	// The glint UI images with UiImage::Glint get (UiRenderFrame::Glint).
	struct UiGlint
	{
		std::uint32_t Mode = 0;								   // 0 off, 1 procedural, 2 texture (Engine::GpuGlint::Source.x).
		std::array<float, 4> Color{ 0.55f, 0.3f, 1.0f, 0.3f }; // rgb; a: intensity.
		std::uint32_t Texture = 0;							   // Bindless glint texture (Texture mode).
		std::uint32_t Sampler = 0;							   // Bindless repeating sampler (Texture mode).
		float Time = 0.0f;									   // Seconds.
	};

	// GpuUiQuad::Blend: the pipeline a quad is drawn with (the shader ignores it).
	inline constexpr std::uint32_t UiQuadBlendNormal = 0; // Premultiplied over.
	inline constexpr std::uint32_t UiQuadBlendInvert = 1; // OneMinusDstColor, OneMinusSrcColor.

	// GpuUiDrawConstants::Flags.
	inline constexpr std::uint32_t UiDrawWorld = 1; // Canvas mapped through a projection: derivative-based coverage.

	// One UI paint quad in canvas pixels (the target's pixels for screen overlays), drawn as
	// one instance of six vertices. The vertex stage clips Rect to Clip in canvas space
	// (exact for these axis-aligned rectangles, also under a projection, so no fragment is
	// discarded) and remaps the UV to the clipped part.
	struct GpuUiQuad
	{
		float Rect[4] = {};		   // x0, y0, x1, y1 (unclipped).
		float Clip[4] = {};		   // x0, y0, x1, y1.
		float Uv[4] = {};		   // u0, v0 at (x0, y0); u1, v1 at (x1, y1).
		float Color[4] = {};	   // Premultiplied linear RGBA (the tint for images).
		float BorderColor[4] = {}; // Premultiplied linear RGBA.
		// Solid: corner radius and inner border width in pixels. Other kinds: Radius is a
		// shear (x += Radius x (Border - y), Border a canvas y), applied after the vertical
		// clip; the horizontal clip is then applied per fragment.
		float Radius = 0.0f;
		float Border = 0.0f;
		float PixelRange = 0.0f; // Glyph, screen overlays: screen pixels per unit of normalized distance (>= 1).
		std::uint32_t Kind = UiQuadSolid;
		std::uint32_t Texture = 0; // Bindless texture index (glyph page or image).
		std::uint32_t Sampler = 0; // Bindless sampler index.
		float UnitRange = 0.0f;	   // Glyph: distance range / atlas page size (UV units), for world canvases.
		std::uint32_t Blend = UiQuadBlendNormal;
	};

	static_assert(sizeof(GpuUiQuad) == 112);
	static_assert(offsetof(GpuUiQuad, Radius) == 80 && offsetof(GpuUiQuad, Kind) == 92 && offsetof(GpuUiQuad, Sampler) == 100 &&
		offsetof(GpuUiQuad, UnitRange) == 104);

	// Push constants of SwimUiQuad (vertex and fragment stages).
	struct GpuUiDrawConstants
	{
		// Canvas pixels -> clip, row-major (clip = M * (x, y, 0, 1)). Screen overlays map
		// pixels 1:1 (+Y down to the RHI's +Y-up clip space); world canvases project.
		float ClipFromCanvas[16] = {};
		float TargetSize[2] = {};	// Framebuffer pixels of the drawn mip.
		std::uint32_t Encoding = 0; // UiOutputEncoding.
		float WhiteScale = 1.0f;	// Linear/scRGB: multiplier; HDR10: nits of UI white.
		std::uint32_t Flags = 0;	// UiDrawWorld.
		float Opacity = 1.0f;		// Canvas fade, multiplies every premultiplied color.
		float Glint[2] = {};		// UiQuadImageGlint: time (s), bindless repeating sampler index (as a float).
	};

	static_assert(sizeof(GpuUiDrawConstants) == 96);
	static_assert(offsetof(GpuUiDrawConstants, TargetSize) == 64 && offsetof(GpuUiDrawConstants, Flags) == 80);

} // namespace Swim::Render
