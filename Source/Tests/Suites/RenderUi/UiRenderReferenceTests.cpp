#include "Engine/Systems/Renderer/UiRendering/UiRenderReference.h"
#include "Tests/Fixtures/TextFontFixture.h"
#include "Tests/Framework/Test.h"

#include <cmath>
#include <stdexcept>

using namespace Swim;
using namespace Swim::Render;
namespace R = Swim::Render::Ui;

namespace
{
	UI::UiPaintQuad Solid(float x, float y, float w, float h, UI::UiColor color)
	{
		UI::UiPaintQuad quad;
		quad.Bounds = { x, y, w, h };
		quad.Clip = { 0, 0, 10000, 10000 };
		quad.Color = color;
		return quad;
	}

	R::Float4 NoTexture(std::uint32_t, std::uint32_t, float, float)
	{
		return { 1, 1, 1, 1 };
	}
} // namespace

SWIM_TEST("Render.Ui.Reference", "BuildsQuadsInFramebufferPixelsAndCullsEmptyOnes")
{
	std::vector<UI::UiPaintQuad> paint;
	paint.push_back(Solid(10, 20, 30, 40, { 0.5f, 0.25f, 0.0f, 0.5f }));
	paint.back().CornerRadius = 4;
	paint.back().BorderWidth = 1;
	auto clipped = Solid(0, 0, 10, 10, { 1, 1, 1, 1 });
	clipped.Clip = { 20, 20, 5, 5 }; // Disjoint: culled.
	paint.push_back(clipped);
	UI::UiPaintQuad glyph;
	glyph.Kind = UI::UiPaintKind::Glyph;
	glyph.Bounds = { 5, 5, 12, 16 };
	glyph.Clip = { 0, 0, 100, 100 };
	glyph.Color = { 1, 1, 1, 1 };
	glyph.AtlasPage = 1;
	glyph.Uv = { 0.5f, 0.25f, 24.0f / 512.0f, 32.0f / 512.0f };
	glyph.DistanceRange = 4.0f;
	paint.push_back(glyph);
	auto orphan = glyph;
	orphan.AtlasPage = 7; // No texture for this page.
	paint.push_back(orphan);
	UI::UiPaintQuad image;
	image.Kind = UI::UiPaintKind::Image;
	image.Bounds = { 0, 0, 8, 8 };
	image.Clip = { 0, 0, 100, 100 };
	image.Color = { 1, 1, 1, 1 };
	image.Texture = 42;
	image.Sampler = 3;
	image.Uv = { 0, 0, 1, 1 };
	paint.push_back(image);

	const std::uint32_t pages[] = { 11, 12 };
	R::QuadBuildDesc desc;
	desc.DpiScale = 2.0f;
	desc.OffsetX = 100.0f;
	desc.AtlasPageSize = 512;
	desc.AtlasTextures = pages;
	desc.AtlasSampler = 5;
	R::QuadBuildStats stats;
	const auto quads = R::BuildQuads(paint, desc, &stats);
	SWIM_REQUIRE_EQUAL(quads.size(), 3u);
	SWIM_CHECK_EQUAL(stats.Culled, 2u);
	SWIM_CHECK(stats.Solids == 1u && stats.Glyphs == 1u && stats.Images == 1u);
	SWIM_CHECK_NEAR(quads[0].Rect[0], 120.0f, 1e-5f);
	SWIM_CHECK_NEAR(quads[0].Rect[3], 120.0f, 1e-5f);
	SWIM_CHECK_NEAR(quads[0].Radius, 8.0f, 1e-5f);
	SWIM_CHECK_NEAR(quads[0].Border, 2.0f, 1e-5f);
	SWIM_CHECK_EQUAL(quads[1].Kind, UiQuadGlyph);
	SWIM_CHECK_EQUAL(quads[1].Texture, 12u);
	SWIM_CHECK_EQUAL(quads[1].Sampler, 5u);
	// 24 texels drawn over 24 pixels: one pixel per texel, range 4.
	SWIM_CHECK_NEAR(quads[1].PixelRange, 4.0f, 1e-4f);
	SWIM_CHECK_NEAR(quads[1].Uv[2], 0.5f + 24.0f / 512.0f, 1e-6f);
	SWIM_CHECK_EQUAL(quads[2].Texture, 42u);
	SWIM_CHECK_EQUAL(quads[2].Sampler, 3u);
	desc.DpiScale = 0.0f;
	SWIM_CHECK_THROWS(R::BuildQuads(paint, desc), std::invalid_argument);

	// Minified glyphs keep at least one pixel of anti-aliasing range.
	desc.DpiScale = 0.1f;
	const auto tiny = R::BuildQuads(std::span(&paint[2], 1), desc);
	SWIM_CHECK_NEAR(tiny[0].PixelRange, 1.0f, 1e-6f);
}

SWIM_TEST("Render.Ui.Reference", "EncodesForSdrLinearScRgbAndHdr10Targets")
{
	const R::Float4 half{ 0.25f, 0.1f, 0.0f, 0.5f }; // Premultiplied (0.5, 0.2, 0, 0.5).
	UiCompositionSettings settings;
	const auto sdr = R::Encode(half, R::BuildDrawConstants(4, 4, settings));
	SWIM_CHECK_NEAR(sdr[0], R::SrgbOetf(0.5f) * 0.5f, 1e-6f);
	SWIM_CHECK_NEAR(sdr[1], R::SrgbOetf(0.2f) * 0.5f, 1e-6f);
	SWIM_CHECK_NEAR(sdr[3], 0.5f, 1e-6f);
	settings.Encoding = UiOutputEncoding::Linear;
	settings.LinearScale = 2.0f;
	const auto linear = R::Encode(half, R::BuildDrawConstants(4, 4, settings));
	SWIM_CHECK_NEAR(linear[0], 0.5f, 1e-6f);
	settings.Encoding = UiOutputEncoding::ScRgb;
	settings.PaperWhiteNits = 160.0f;
	const auto scrgb = R::Encode(half, R::BuildDrawConstants(4, 4, settings));
	SWIM_CHECK_NEAR(scrgb[0], 0.5f, 1e-6f); // 0.25 x 160 / 80.
	settings.Encoding = UiOutputEncoding::Hdr10;
	settings.PaperWhiteNits = 203.0f;
	const auto white = R::Encode({ 1, 1, 1, 1 }, R::BuildDrawConstants(4, 4, settings));
	SWIM_CHECK_NEAR(white[0], R::PqOetf(203.0f), 1e-5f); // BT.2408 reference white ~0.58.
	SWIM_CHECK(white[0] > 0.57f && white[0] < 0.59f);
	SWIM_CHECK_NEAR(R::Encode({ 0, 0, 0, 0 }, R::BuildDrawConstants(4, 4, settings))[3], 0.0f, 1e-7f);
	settings.PaperWhiteNits = 0.0f;
	SWIM_CHECK_THROWS(R::BuildDrawConstants(4, 4, settings), std::invalid_argument);
	settings = {};
	settings.Encoding = static_cast<UiOutputEncoding>(9);
	SWIM_CHECK_THROWS(ValidateUiCompositionSettings(settings), std::invalid_argument);
	// Premultiplied over.
	const auto blended = R::Blend({ 0, 0, 1, 1 }, { 0.5f, 0, 0, 0.5f });
	SWIM_CHECK_NEAR(blended[0], 0.5f, 1e-6f);
	SWIM_CHECK_NEAR(blended[2], 0.5f, 1e-6f);
	SWIM_CHECK_NEAR(blended[3], 1.0f, 1e-6f);
}

SWIM_TEST("Render.Ui.Reference", "RoundedBordersClipAndRasterizeInPaintOrder")
{
	GpuUiQuad quad;
	quad.Rect[0] = 0;
	quad.Rect[1] = 0;
	quad.Rect[2] = 40;
	quad.Rect[3] = 20;
	quad.Clip[0] = quad.Clip[1] = 0;
	quad.Clip[2] = quad.Clip[3] = 1000;
	quad.Color[0] = quad.Color[3] = 1.0f;
	quad.BorderColor[2] = quad.BorderColor[3] = 1.0f;
	quad.Radius = 8.0f;
	quad.Border = 2.0f;
	const auto centre = R::ShadeQuad(quad, 20.5f, 10.5f, NoTexture);
	SWIM_CHECK_NEAR(centre[0], 1.0f, 1e-6f); // Fill.
	const auto ring = R::ShadeQuad(quad, 20.5f, 0.5f, NoTexture);
	SWIM_CHECK_NEAR(ring[2], 1.0f, 1e-6f); // Border (top edge).
	SWIM_CHECK_NEAR(ring[0], 0.0f, 1e-6f);
	const auto corner = R::ShadeQuad(quad, 0.5f, 0.5f, NoTexture);
	SWIM_CHECK_NEAR(corner[3], 0.0f, 1e-6f); // Outside the rounded corner.
	SWIM_CHECK_NEAR(R::RoundedBoxDistance(20, 10, 20, 10, 20, 10, 8), -10.0f, 1e-5f);
	quad.Clip[0] = 30.0f;
	SWIM_CHECK_NEAR(R::ShadeQuad(quad, 20.5f, 10.5f, NoTexture)[3], 0.0f, 1e-6f); // Clipped away.

	// Paint order: later quads blend over earlier ones; clips are exact at pixel centres.
	GpuUiQuad back;
	back.Rect[2] = back.Rect[3] = 4;
	back.Clip[2] = back.Clip[3] = 4;
	back.Color[2] = back.Color[3] = 1.0f;
	GpuUiQuad front = back;
	front.Color[2] = 0.0f;
	front.Color[0] = 0.5f;
	front.Color[3] = 0.5f;
	front.Clip[0] = 2.0f; // Right half only.
	R::Canvas canvas{ 4, 4, std::vector<R::Float4>(16) };
	UiCompositionSettings linear;
	linear.Encoding = UiOutputEncoding::Linear;
	const GpuUiQuad quads[] = { back, front };
	R::Rasterize(canvas, quads, R::BuildDrawConstants(4, 4, linear), NoTexture);
	SWIM_CHECK_NEAR(canvas.Texels[0][2], 1.0f, 1e-6f); // Left half: only the blue quad.
	SWIM_CHECK_NEAR(canvas.Texels[2][0], 0.5f, 1e-6f); // Right half: red over blue.
	SWIM_CHECK_NEAR(canvas.Texels[2][2], 0.5f, 1e-6f);
	SWIM_CHECK_NEAR(canvas.Texels[2][3], 1.0f, 1e-6f);
}

SWIM_TEST("Render.Ui.Reference", "MsdfGlyphCoverageMatchesTheOutline")
{
	const auto font = Swim::Testing::LoadTextFontFixture();
	Swim::Text::GlyphAtlas atlas;
	const auto entry = atlas.Get(font, font->GetGlyph(U'O'));
	const auto page = atlas.GetPage(entry.Page);
	std::vector<std::uint8_t> rgba(std::size_t(page.Size) * page.Size * 4);
	for (std::size_t i = 0; i < std::size_t(page.Size) * page.Size; ++i)
	{
		rgba[i * 4 + 0] = page.Pixels[i * 3 + 0];
		rgba[i * 4 + 1] = page.Pixels[i * 3 + 1];
		rgba[i * 4 + 2] = page.Pixels[i * 3 + 2];
		rgba[i * 4 + 3] = 255;
	}
	// Draw the glyph at 4x its atlas size.
	UI::UiPaintQuad quad;
	quad.Kind = UI::UiPaintKind::Glyph;
	quad.Bounds = { 0, 0, float(entry.Width) * 4.0f, float(entry.Height) * 4.0f };
	quad.Clip = { 0, 0, 10000, 10000 };
	quad.Color = { 1, 1, 1, 1 };
	quad.AtlasPage = 0;
	quad.Uv = { float(entry.X) / page.Size, float(entry.Y) / page.Size, float(entry.Width) / page.Size, float(entry.Height) / page.Size };
	quad.DistanceRange = atlas.GetDesc().DistanceRange;
	const std::uint32_t textures[] = { 1 };
	R::QuadBuildDesc desc;
	desc.AtlasPageSize = page.Size;
	desc.AtlasTextures = textures;
	const auto quads = R::BuildQuads(std::span(&quad, 1), desc);
	SWIM_REQUIRE_EQUAL(quads.size(), 1u);
	SWIM_CHECK_NEAR(quads[0].PixelRange, 16.0f, 1e-4f);
	const auto sample = [&](std::uint32_t, std::uint32_t, float u, float v)
	{
		return R::SampleBilinear(rgba, page.Size, page.Size, u, v);
	};
	const float w = quad.Bounds.Width;
	const float h = quad.Bounds.Height;
	// The hole of the O and the outside are empty; the left stroke is solid.
	SWIM_CHECK_NEAR(R::ShadeQuad(quads[0], w * 0.5f, h * 0.5f, sample)[3], 0.0f, 1e-3f);
	SWIM_CHECK_NEAR(R::ShadeQuad(quads[0], 1.5f, 1.5f, sample)[3], 0.0f, 1e-3f);
	float stroke = 0.0f;
	for (float x = 0.5f; x < w * 0.5f; x += 1.0f)
	{
		stroke = std::max(stroke, R::ShadeQuad(quads[0], x, h * 0.5f, sample)[3]);
	}
	SWIM_CHECK_NEAR(stroke, 1.0f, 1e-3f);
	SWIM_CHECK_NEAR(R::Median(0.2f, 0.9f, 0.5f), 0.5f, 1e-7f);
}
