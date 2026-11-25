#include "Engine/Systems/Renderer/UiRendering/UiRenderReference.h"
#include "Tests/Fixtures/BitmapFontFixture.h"
#include "Tests/Framework/Test.h"

#include <algorithm>
#include <cmath>

using namespace Swim;
using namespace Swim::Render;
namespace R = Swim::Render::Ui;
using Swim::Testing::MakeBitmapFontFixture;

namespace
{

	// Nearest sampling of RGBA8 atlas pages (texture index = page), as the point sampler does.
	struct PageSampler
	{
		const Text::GlyphAtlas& Atlas;

		R::Float4 operator()(std::uint32_t texture, std::uint32_t, float u, float v) const
		{
			const auto page = Atlas.GetPage(texture);
			const int x = std::clamp(int(std::floor(u * float(page.Size))), 0, int(page.Size) - 1);
			const int y = std::clamp(int(std::floor(v * float(page.Size))), 0, int(page.Size) - 1);
			const std::size_t i = (std::size_t(y) * page.Size + std::size_t(x)) * 4;
			return { page.Pixels[i] / 255.0f, page.Pixels[i + 1] / 255.0f, page.Pixels[i + 2] / 255.0f, page.Pixels[i + 3] / 255.0f };
		}
	};

	// Opaque columns of a rasterized row band (any alpha > 0.5).
	std::vector<int> Columns(const R::Canvas& canvas, int y0, int y1)
	{
		std::vector<int> columns;

		for (int x = 0; x < int(canvas.Width); ++x)
		{
			for (int y = y0; y < y1; ++y)
			{
				if (canvas.Texels[std::size_t(y) * canvas.Width + std::size_t(x)][3] > 0.5f)
				{
					columns.push_back(x);
					break;
				}
			}
		}

		return columns;
	}

	struct Rendered
	{
		R::Canvas Canvas;
		UI::UiRect Bounds;
	};

	Rendered RenderText(float size, const UI::UiTextOptions& options, std::string text, UI::UiRect clip = { 0, 0, 0, 0 })
	{
		UI::UiDocument ui;
		const auto label = ui.Create(ui.GetRoot());
		UI::UiStyle style;
		style.Absolute = true;
		style.Offset = { 2, 1 };

		if (clip.Width > 0)
		{
			style.Clip = true;
			style.Width = UI::UiLength::Pixels(clip.Width);
		}

		ui.SetStyle(label, style);
		ui.SetText(label, Text::FontCollection::Single(MakeBitmapFontFixture()), std::move(text), size, options);
		ui.Layout({ 160, 40 });
		Text::GlyphAtlas atlas({ 128, 4, 32, 4 });
		const auto& paint = ui.Paint(atlas);
		std::vector<std::uint32_t> pages;

		for (std::uint32_t i = 0; i < atlas.GetPageCount(); ++i)
		{
			pages.push_back(i);
		}

		R::QuadBuildDesc desc;
		desc.AtlasPageSize = 128;
		desc.AtlasTextures = pages;
		desc.AtlasPointSampler = 9;
		const auto quads = R::BuildQuads(paint, desc);
		Rendered result;
		result.Canvas.Width = 160;
		result.Canvas.Height = 40;
		result.Canvas.Texels.assign(160 * 40, {});
		R::Rasterize(result.Canvas, quads, R::BuildDrawConstants(160, 40, {}), PageSampler{ atlas });
		result.Bounds = ui.GetBounds(label);
		return result;
	}

} // namespace

SWIM_TEST("Render.Ui.BitmapText", "ColorGlyphQuadsUseThePointSamplerAndDecodeSrgb")
{
	UI::UiPaintQuad glyph;
	glyph.Kind = UI::UiPaintKind::ColorGlyph;
	glyph.Bounds = { 0, 0, 4, 4 };
	glyph.Clip = { 0, 0, 100, 100 };
	glyph.Color = { 1, 1, 1, 1 };
	glyph.AtlasPage = 0;
	glyph.Uv = { 0, 0, 0.5f, 0.5f };
	const std::uint32_t pages[] = { 33 };
	R::QuadBuildDesc desc;
	desc.AtlasPageSize = 8;
	desc.AtlasTextures = pages;
	desc.AtlasSampler = 4;
	desc.AtlasPointSampler = 6;
	const auto quads = R::BuildQuads(std::span(&glyph, 1), desc);
	SWIM_REQUIRE_EQUAL(quads.size(), 1u);
	SWIM_CHECK_EQUAL(quads[0].Kind, UiQuadColorGlyph);
	SWIM_CHECK_EQUAL(quads[0].Texture, 33u);
	SWIM_CHECK_EQUAL(quads[0].Sampler, 6u);
	const auto half = R::ShadeQuad(quads[0], 1.5f, 1.5f,
		[](std::uint32_t, std::uint32_t, float, float)
		{
			return R::Float4{ 0.5f, 1.0f, 0.0f, 0.5f };
		});
	SWIM_CHECK_NEAR(half[0], R::SrgbEotf(0.5f) * 0.5f, 1e-6f); // Straight sRGB -> premultiplied linear.
	SWIM_CHECK_NEAR(half[1], 0.5f, 1e-6f);
	SWIM_CHECK_NEAR(half[3], 0.5f, 1e-6f);
}

SWIM_TEST("Render.Ui.BitmapText", "RasterizedTextCoversExactlyTheMeasuredPixelColumns")
{
	for (const float scale : { 1.0f, 2.0f, 3.0f })
	{
		const auto r = RenderText(8.0f * scale, {}, "AiAl");
		// A 0-4, i 6, A 8-12, l 14-15 in font pixels from x = 2.
		std::vector<int> expected;

		for (const int column : { 0, 1, 2, 3, 4, 6, 8, 9, 10, 11, 12, 14, 15 })
		{
			for (int k = 0; k < int(scale); ++k)
			{
				expected.push_back(2 + column * int(scale) + k);
			}
		}

		SWIM_CHECK(Columns(r.Canvas, 0, 40) == expected);
		SWIM_CHECK_NEAR(r.Bounds.Width, (6 + 2 + 6 + 3) * scale, 1e-4f);
		// Rows 1-6 of each 8-row cell are opaque: rows [1 + s, 1 + 7s) of the canvas.
		const auto& texels = r.Canvas.Texels;
		SWIM_CHECK(texels[std::size_t(1 + int(scale)) * 160 + 2][3] > 0.5f);
		SWIM_CHECK(texels[std::size_t(1 + int(scale) - 1) * 160 + 2][3] < 0.5f);
		SWIM_CHECK(texels[std::size_t(1 + 7 * int(scale) - 1) * 160 + 2][3] > 0.5f);
		SWIM_CHECK(texels[std::size_t(1 + 7 * int(scale)) * 160 + 2][3] < 0.5f);
	}
}

SWIM_TEST("Render.Ui.BitmapText", "BoldAddsOneColumnAndItalicStaysInsideItsClip")
{
	UI::UiTextOptions bold;
	bold.Styles = { { 0, { 1, 1, 1, 1 }, UI::UiTextFormat::Bold } };
	const auto r = RenderText(8.0f, bold, "i");
	SWIM_CHECK(Columns(r.Canvas, 0, 40) == (std::vector<int>{ 2, 3 }));
	SWIM_CHECK_NEAR(r.Bounds.Width, 3.0f, 1e-5f);

	UI::UiTextOptions italic;
	italic.Styles = { { 0, { 1, 1, 1, 1 }, UI::UiTextFormat::Italic } };
	const auto upright = RenderText(16.0f, {}, "W");
	const auto slanted = RenderText(16.0f, italic, "W");
	// The glyph's top row shifts right, its baseline row stays.
	const auto topUpright = Columns(upright.Canvas, 3, 4);
	const auto topSlanted = Columns(slanted.Canvas, 3, 4);
	SWIM_REQUIRE(!topUpright.empty() && !topSlanted.empty());
	SWIM_CHECK(topSlanted.front() > topUpright.front());
	// Clipped to 6 units: no sheared pixel escapes the clip on the right.
	const auto clipped = RenderText(16.0f, italic, "W", { 0, 0, 6, 0 });
	const auto columns = Columns(clipped.Canvas, 0, 40);
	SWIM_REQUIRE(!columns.empty());
	SWIM_CHECK(columns.back() < 2 + 6);
}
