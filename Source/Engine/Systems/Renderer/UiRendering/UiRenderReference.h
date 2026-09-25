#pragma once
#include "Engine/Systems/Renderer/UiRendering/UiRenderRecords.h"
#include "Engine/Systems/Renderer/UiRendering/UiRenderSettings.h"
#include "Engine/Systems/UI/UiDocument.h"

#include <array>
#include <functional>
#include <optional>
#include <span>
#include <vector>

// CPU definition of the UI renderer (critical-path item 79): how paint quads become
// GpuUiQuads, and every rule SwimUiQuad evaluates, so tests can compare GPU images
// with an exact CPU rasterization.
namespace Swim::Render::Ui
{
	using Float4 = std::array<float, 4>;

	struct QuadBuildDesc
	{
		float DpiScale = 1.0f; // Logical units -> framebuffer pixels.
		float OffsetX = 0.0f;  // Framebuffer pixels added to every position.
		float OffsetY = 0.0f;
		std::uint32_t AtlasPageSize = 0;			  // Glyph atlas page size in texels.
		std::span<const std::uint32_t> AtlasTextures; // Bindless texture index per atlas page.
		std::uint32_t AtlasSampler = 0;				  // Bindless sampler index for atlas pages.
	};

	struct QuadBuildStats
	{
		std::uint32_t Solids = 0;
		std::uint32_t Glyphs = 0;
		std::uint32_t Images = 0;
		std::uint32_t Culled = 0; // Empty after clipping, or glyphs of pages without a texture.
	};

	// Paint order is preserved. Rectangles are scaled by DpiScale and offset; glyph
	// PixelRange = max(1, DistanceRange x screen pixels per atlas texel) and UnitRange =
	// DistanceRange / AtlasPageSize.
	std::vector<GpuUiQuad> BuildQuads(std::span<const UI::UiPaintQuad> paint, const QuadBuildDesc& desc, QuadBuildStats* stats = nullptr);

	// A screen overlay of a width x height target: canvas pixels are target pixels.
	GpuUiDrawConstants BuildDrawConstants(
		std::uint32_t width, std::uint32_t height, const UiCompositionSettings& settings, float opacity = 1.0f);
	// A canvas seen through clipFromCanvas (UI::ClipFromCanvas): UiDrawWorld. Throws
	// std::invalid_argument for a non-finite matrix or an opacity outside [0, 1].
	GpuUiDrawConstants BuildCanvasDrawConstants(std::uint32_t width, std::uint32_t height, const UiCompositionSettings& settings,
		const std::array<float, 16>& clipFromCanvas, float opacity = 1.0f);

	float SrgbOetf(float linear);
	float PqOetf(float nits); // SMPTE ST 2084 inverse EOTF.
	std::array<float, 3> Rec709ToRec2020(const std::array<float, 3>& color);
	float Median(float r, float g, float b);
	// Signed distance to a rounded box (negative inside), all in pixels.
	float RoundedBoxDistance(float px, float py, float centerX, float centerY, float halfX, float halfY, float radius);

	// Samples a bindless texture: (texture, sampler, u, v) -> RGBA.
	using TextureSampler = std::function<Float4(std::uint32_t, std::uint32_t, float, float)>;

	// What a screen pixel covers of a world canvas, as the shader's fwidth: canvas pixels
	// (|d/dx| + |d/dy| per axis) and UV units.
	struct Footprint
	{
		float CanvasX = 1.0f;
		float CanvasY = 1.0f;
		float U = 0.0f;
		float V = 0.0f;
	};

	// The premultiplied linear color a quad contributes at canvas point (px, py), before
	// encoding and blending; transparent outside the clipped rectangle. Without a footprint
	// (screen overlays) ramps are one pixel wide and glyphs use PixelRange; with one
	// (UiDrawWorld) ramps span the footprint and glyph ranges come from UnitRange.
	Float4 ShadeQuad(const GpuUiQuad& quad, float px, float py, const TextureSampler& sample, const Footprint* footprint = nullptr);
	// Output encoding of a premultiplied color (UiOutputEncoding), after the canvas opacity.
	Float4 Encode(const Float4& premultiplied, const GpuUiDrawConstants& constants);
	// Premultiplied source over destination: One, OneMinusSourceAlpha on all channels.
	Float4 Blend(const Float4& destination, const Float4& source);

	struct Canvas
	{
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::vector<Float4> Texels; // Row-major, top-down, stored values.
	};

	// Rasterizes quads in order: a pixel is covered when its centre lies in the clipped
	// rectangle [x0, x1) x [y0, y1) (the top-left rule for axis-aligned edges). Screen
	// overlays (canvas pixels are target pixels).
	void Rasterize(Canvas& canvas, std::span<const GpuUiQuad> quads, const GpuUiDrawConstants& constants, const TextureSampler& sample);

	// The canvas point seen at a target pixel centre through ClipFromCanvas (the inverse
	// homography of the canvas plane) and its footprint for a quad's UV scale; empty where
	// the plane is behind the camera or seen edge-on.
	struct CanvasSample
	{
		float X = 0.0f;
		float Y = 0.0f;
		float FootprintX = 1.0f; // fwidth of canvas x per screen pixel.
		float FootprintY = 1.0f;
	};

	std::optional<CanvasSample> CanvasAt(const GpuUiDrawConstants& constants, float px, float py);

	// World canvases (UiDrawWorld): each target pixel centre is mapped back to the canvas
	// and shaded with its analytic footprint. Depth is not modelled (a front-facing canvas
	// in front of everything); pixels are covered where their canvas point lies inside the
	// clipped rectangle.
	void RasterizeProjected(
		Canvas& canvas, std::span<const GpuUiQuad> quads, const GpuUiDrawConstants& constants, const TextureSampler& sample);

	// Bilinear, clamp-to-edge sampling of a tightly packed RGBA8 UNORM image with
	// Vulkan's texel-centre convention.
	Float4 SampleBilinear(std::span<const std::uint8_t> rgba, std::uint32_t width, std::uint32_t height, float u, float v);
} // namespace Swim::Render::Ui
