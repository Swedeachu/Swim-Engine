#include "Engine/Systems/UI/UiDocument.h"
#include "Tests/Fixtures/BitmapFontFixture.h"
#include "Tests/Fixtures/TextFontFixture.h"
#include "Tests/Framework/Test.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace Swim::UI;
using Swim::Testing::MakeBitmapFontFixture;

namespace
{

	std::vector<UiPaintQuad> Of(const std::vector<UiPaintQuad>& paint, UiPaintKind kind)
	{
		std::vector<UiPaintQuad> result;
		std::copy_if(paint.begin(), paint.end(), std::back_inserter(result),
			[&](const UiPaintQuad& quad)
			{
				return quad.Kind == kind;
			});
		return result;
	}

	struct Fixture
	{
		UiDocument Ui;
		UiNodeId Label;
		Swim::Text::GlyphAtlas Atlas{ { 128, 8, 32, 4 } };
		std::shared_ptr<const Swim::Text::FontCollection> Fonts = Swim::Text::FontCollection::Single(MakeBitmapFontFixture());

		Fixture()
		{
			auto root = Ui.GetStyle(Ui.GetRoot());
			root.Flow = UiFlow::Row;
			root.AlignItems = UiAlign::Start;
			Ui.SetStyle(Ui.GetRoot(), root);
			Label = Ui.Create(Ui.GetRoot());
			UiStyle style;
			style.Margin = { 10.25f, 3.0f, 0, 0 }; // A fractional origin: bitmap glyphs snap.
			Ui.SetStyle(Label, style);
		}
	};

} // namespace

SWIM_TEST("UI.RichText", "BitmapMeasurementMatchesTheDrawnGlyphs")
{
	for (const float scale : { 1.0f, 2.0f, 3.0f })
	{
		Fixture f;
		UiTextOptions options;
		options.Styles = { { 0, { 1, 1, 1, 1 }, UiTextFormat::None }, { 4, { 1, 0, 0, 1 }, UiTextFormat::Bold } };
		f.Ui.SetText(f.Label, f.Fonts, "Ail AW", 8.0f * scale, options);
		f.Ui.Layout({ 400, 100 });
		const auto bounds = f.Ui.GetBounds(f.Label);
		// A6 i2 l3 space4, then bold A7 W9.
		SWIM_CHECK_NEAR(bounds.Width, (6 + 2 + 3 + 4 + 7 + 9) * scale, 1e-4f);
		SWIM_CHECK_NEAR(bounds.Height, 9.0f * scale, 1e-4f);
		const auto glyphs = Of(f.Ui.Paint(f.Atlas), UiPaintKind::ColorGlyph);
		SWIM_REQUIRE_EQUAL(glyphs.size(), 3u + 4u); // Bold glyphs draw twice.
		const float x0 = std::round(10.25f);
		const float pens[] = { 0, 6, 8, 15, 15 + 1, 22, 22 + 1 };
		const float widths[] = { 5, 1, 2, 5, 5, 7, 7 };

		for (std::size_t i = 0; i < glyphs.size(); ++i)
		{
			SWIM_CHECK_NEAR(glyphs[i].Bounds.X, x0 + pens[i] * scale, 1e-4f);
			SWIM_CHECK_NEAR(glyphs[i].Bounds.Width, widths[i] * scale, 1e-4f);
			SWIM_CHECK_NEAR(glyphs[i].Bounds.Y, 3.0f, 1e-4f); // The cell top: baseline 7 below the line's top.
			SWIM_CHECK_NEAR(glyphs[i].Bounds.Height, 8.0f * scale, 1e-4f);
		}

		// Every drawn texel lies inside the measured box (the bold copy ends at the advance).
		const float right = glyphs.back().Bounds.X + glyphs.back().Bounds.Width;
		SWIM_CHECK(right <= x0 + bounds.Width + 1e-4f);
		SWIM_CHECK_NEAR(glyphs[3].Color.G, 0.0f, 1e-6f); // The red span.
		SWIM_CHECK_NEAR(glyphs[0].Color.G, 1.0f, 1e-6f);
	}
}

SWIM_TEST("UI.RichText", "WrappedLinesMatchMeasurement")
{
	Fixture f;
	auto style = f.Ui.GetStyle(f.Label);
	style.Width = UiLength::Pixels(20); // One 12-unit word per line.
	style.TextWrap = Swim::Text::TextWrap::Word;
	f.Ui.SetStyle(f.Label, style);
	f.Ui.SetText(f.Label, f.Fonts, "AA AA AA", 8.0f);
	f.Ui.Layout({ 400, 100 });
	SWIM_CHECK_NEAR(f.Ui.GetBounds(f.Label).Height, 27.0f, 1e-4f);
	const auto glyphs = Of(f.Ui.Paint(f.Atlas), UiPaintKind::ColorGlyph);
	SWIM_REQUIRE_EQUAL(glyphs.size(), 6u);
	const auto* layout = f.Ui.GetTextLayout(f.Label);
	SWIM_REQUIRE(layout != nullptr);
	SWIM_CHECK_EQUAL(layout->GetLines().size(), 3u);
	std::size_t drawn = 0;

	for (const auto& glyph : layout->GetGlyphs())
	{
		if (glyph.Glyph != U'A')
		{
			continue;
		}

		SWIM_REQUIRE(drawn < glyphs.size());
		SWIM_CHECK_NEAR(glyphs[drawn].Bounds.X, std::round(10.25f + glyph.X), 1e-4f);
		SWIM_CHECK_NEAR(glyphs[drawn].Bounds.Y, 3.0f + glyph.Y - 7.0f, 1e-4f);
		++drawn;
	}

	SWIM_CHECK_EQUAL(drawn, 6u);
}

SWIM_TEST("UI.RichText", "SpansShadowUnderlineStrikeAndItalic")
{
	Fixture f;
	UiTextOptions options;
	options.Styles = { { 0, { 1, 1, 1, 1 }, UiTextFormat::Shadow | UiTextFormat::Underline },
		{ 2, { 1, 1, 1, 1 }, UiTextFormat::Italic | UiTextFormat::Strikethrough } };
	f.Ui.SetText(f.Label, f.Fonts, "AAAA", 16.0f, options);
	f.Ui.Layout({ 400, 100 });
	const auto revision = f.Ui.GetLayoutRevision();
	const auto& paint = f.Ui.Paint(f.Atlas);
	const auto glyphs = Of(paint, UiPaintKind::ColorGlyph);
	const auto solids = Of(paint, UiPaintKind::Solid);
	SWIM_REQUIRE_EQUAL(glyphs.size(), 2u + 4u);
	// Shadows first, offset by one font pixel (2 units at 2x), quarter brightness.
	SWIM_CHECK_NEAR(glyphs[0].Bounds.X, glyphs[2].Bounds.X + 2.0f, 1e-4f);
	SWIM_CHECK_NEAR(glyphs[0].Bounds.Y, glyphs[2].Bounds.Y + 2.0f, 1e-4f);
	SWIM_CHECK_NEAR(glyphs[0].Color.R, 0.0508761f, 1e-5f);
	SWIM_CHECK_NEAR(glyphs[2].Shear, 0.0f, 1e-6f);
	SWIM_CHECK_NEAR(glyphs[4].Shear, 0.25f, 1e-6f);
	SWIM_CHECK_NEAR(glyphs[4].ShearPivotY, glyphs[4].Bounds.Y + 14.0f, 1e-4f); // On the baseline.
	// One merged underline under the shadowed pair (and its shadow), one strike over the rest.
	SWIM_REQUIRE_EQUAL(solids.size(), 3u);
	SWIM_CHECK_NEAR(solids[0].Bounds.Width, 24.0f, 1e-4f);
	SWIM_CHECK_NEAR(solids[1].Bounds.Width, 24.0f, 1e-4f);
	SWIM_CHECK_NEAR(solids[1].Bounds.Height, 2.0f, 1e-4f);
	SWIM_CHECK_NEAR(solids[1].Bounds.Y, glyphs[2].Bounds.Y + 14.0f + 2.0f, 1e-4f);
	SWIM_CHECK_NEAR(solids[2].Bounds.X, solids[1].Bounds.X + 24.0f, 1e-4f);
	SWIM_CHECK_NEAR(solids[2].Bounds.Y, glyphs[2].Bounds.Y + 14.0f - 8.0f, 1e-4f);
	// Recolouring and re-formatting without bold changes is paint-only.
	options.Styles[1].Color = { 0, 1, 0, 1 };
	options.Styles[1].Format = UiTextFormat::Underline;
	f.Ui.SetText(f.Label, f.Fonts, "AAAA", 16.0f, options);
	SWIM_CHECK(f.Ui.IsLayoutCurrent());
	f.Ui.Layout({ 400, 100 });
	SWIM_CHECK_EQUAL(f.Ui.GetLayoutRevision(), revision);
	options.Styles[1].Format = UiTextFormat::Bold;
	f.Ui.SetText(f.Label, f.Fonts, "AAAA", 16.0f, options);
	SWIM_CHECK(!f.Ui.IsLayoutCurrent());
	options.Styles = { { 2, {}, UiTextFormat::None }, { 1, {}, UiTextFormat::None } };
	SWIM_CHECK_THROWS(f.Ui.SetText(f.Label, f.Fonts, "AAAA", 16.0f, options), std::invalid_argument);
}

SWIM_TEST("UI.RichText", "ObfuscatedTextRepaintsWithSameWidthGlyphs")
{
	Fixture f;
	UiTextOptions options;
	options.Styles = { { 0, { 1, 1, 1, 1 }, UiTextFormat::Obfuscated } };
	f.Ui.SetText(f.Label, f.Fonts, "llll", 8.0f, options);
	f.Ui.Layout({ 400, 100 });
	f.Ui.Paint(f.Atlas);
	bool changed = false;
	std::vector<float> first;

	for (int frame = 0; frame < 8; ++frame)
	{
		const auto glyphs = Of(f.Ui.Paint(f.Atlas), UiPaintKind::ColorGlyph);
		SWIM_REQUIRE_EQUAL(glyphs.size(), 4u);
		SWIM_CHECK_EQUAL(f.Ui.GetRepaintedNodeCount(), 1u);
		std::vector<float> us;

		for (const auto& glyph : glyphs)
		{
			us.push_back(glyph.Uv.X);
		}

		if (first.empty())
		{
			first = us;
		}

		changed = changed || us != first;
		SWIM_CHECK_NEAR(glyphs[1].Bounds.X - glyphs[0].Bounds.X, 3.0f, 1e-4f);
	}

	SWIM_CHECK(changed); // 'l' and '|' share the advance.
}

SWIM_TEST("UI.RichText", "VectorFallbackAndColorSpansCombine")
{
	Fixture f;
	const auto fonts = std::make_shared<const Swim::Text::FontCollection>(std::vector<std::shared_ptr<const Swim::Text::FontFace>>{
		MakeBitmapFontFixture(), Swim::Testing::LoadTextFontFixture() });
	UiTextOptions options;
	options.Colors = { { 0, { 0, 0, 1, 1 } } };
	options.Styles = { { 0, { 1, 1, 1, 0.5f }, UiTextFormat::None } };
	f.Ui.SetText(f.Label, fonts, "A\xC3\xA9", 16.0f, options);
	f.Ui.Layout({ 400, 100 });
	const auto& paint = f.Ui.Paint(f.Atlas);
	const auto bitmap = Of(paint, UiPaintKind::ColorGlyph);
	const auto vector = Of(paint, UiPaintKind::Glyph);
	SWIM_REQUIRE_EQUAL(bitmap.size(), 1u);
	SWIM_REQUIRE_EQUAL(vector.size(), 1u);
	SWIM_CHECK_NEAR(bitmap[0].Color.B, 0.5f, 1e-6f); // Blue span, halved by the style.
	SWIM_CHECK_NEAR(bitmap[0].Color.A, 0.5f, 1e-6f);
	SWIM_CHECK_NEAR(vector[0].Color.B, 0.5f, 1e-6f);
}
