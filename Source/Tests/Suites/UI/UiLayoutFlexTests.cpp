#include "Engine/Systems/UI/UiDocument.h"
#include "Tests/Fixtures/TextFontFixture.h"
#include "Tests/Framework/Test.h"

#include <stdexcept>

using namespace Swim::UI;

namespace
{
	UiStyle Box(float width, float height)
	{
		UiStyle style;
		style.Width = UiLength::Pixels(width);
		style.Height = UiLength::Pixels(height);
		return style;
	}

	UiNodeId Child(UiDocument& ui, UiNodeId parent, const UiStyle& style)
	{
		const auto id = ui.Create(parent);
		ui.SetStyle(id, style);
		return id;
	}
} // namespace

SWIM_TEST("UI.Layout", "RowsGrowShrinkJustifyAndAlignChildren")
{
	UiDocument ui;
	auto rowStyle = Box(300, 100);
	rowStyle.Flow = UiFlow::Row;
	rowStyle.Gap = 10;
	rowStyle.AlignItems = UiAlign::Center;
	const auto row = Child(ui, ui.GetRoot(), rowStyle);
	auto a = Box(50, 20);
	a.Grow = 1;
	auto b = Box(50, 40);
	b.Grow = 2;
	auto c = Box(50, 60);
	c.AlignSelf = UiAlign::End;
	const auto first = Child(ui, row, a);
	const auto second = Child(ui, row, b);
	const auto third = Child(ui, row, c);
	ui.Layout({ 500, 500 });
	// Free space 300 - 150 - 20 = 130: 1/3 to the first, 2/3 to the second.
	SWIM_CHECK_NEAR(ui.GetBounds(first).Width, 50.0f + 130.0f / 3.0f, 1e-3f);
	SWIM_CHECK_NEAR(ui.GetBounds(second).Width, 50.0f + 260.0f / 3.0f, 1e-3f);
	SWIM_CHECK_NEAR(ui.GetBounds(third).X + ui.GetBounds(third).Width, 300.0f, 1e-3f);
	SWIM_CHECK_NEAR(ui.GetBounds(first).Y, 40.0f, 1e-3f); // Centered: (100 - 20) / 2.
	SWIM_CHECK_NEAR(ui.GetBounds(third).Y, 40.0f, 1e-3f); // End: 100 - 60.
	SWIM_CHECK_NEAR(ui.GetBounds(second).Y, 30.0f, 1e-3f);

	// Without grow, justification distributes the free space.
	a.Grow = b.Grow = 0;
	ui.SetStyle(first, a);
	ui.SetStyle(second, b);
	rowStyle.Justify = UiJustify::SpaceBetween;
	ui.SetStyle(row, rowStyle);
	ui.Layout({ 500, 500 });
	SWIM_CHECK_NEAR(ui.GetBounds(first).X, 0.0f, 1e-3f);
	SWIM_CHECK_NEAR(ui.GetBounds(second).X, 125.0f, 1e-3f); // 50 + 10 + 65.
	SWIM_CHECK_NEAR(ui.GetBounds(third).X, 250.0f, 1e-3f);
	rowStyle.Justify = UiJustify::Center;
	ui.SetStyle(row, rowStyle);
	ui.Layout({ 500, 500 });
	SWIM_CHECK_NEAR(ui.GetBounds(first).X, 65.0f, 1e-3f);
	rowStyle.Justify = UiJustify::SpaceEvenly;
	ui.SetStyle(row, rowStyle);
	ui.Layout({ 500, 500 });
	SWIM_CHECK_NEAR(ui.GetBounds(first).X, 32.5f, 1e-3f); // 130 / 4.

	// Stretch fills the cross axis of Auto children; shrink absorbs an overflow.
	rowStyle.Justify = UiJustify::Start;
	rowStyle.AlignItems = UiAlign::Stretch;
	rowStyle.Width = UiLength::Pixels(100);
	ui.SetStyle(row, rowStyle);
	a.Height = {};
	a.Shrink = 1;
	b.Shrink = 1;
	c.AlignSelf = UiAlign::Auto;
	ui.SetStyle(first, a);
	ui.SetStyle(second, b);
	ui.SetStyle(third, c);
	ui.Layout({ 500, 500 });
	SWIM_CHECK_NEAR(ui.GetBounds(first).Height, 100.0f, 1e-3f);
	SWIM_CHECK_NEAR(ui.GetBounds(second).Height, 40.0f, 1e-3f); // Fixed heights do not stretch.
	// Overflow 70: shared by the first two (third does not shrink).
	SWIM_CHECK_NEAR(ui.GetBounds(first).Width, 15.0f, 1e-3f);
	SWIM_CHECK_NEAR(ui.GetBounds(third).Width, 50.0f, 1e-3f);
}

SWIM_TEST("UI.Layout", "OverlayAlignmentAnchorsPivotsAndAspectRatio")
{
	UiDocument ui;
	auto panelStyle = Box(200, 100);
	panelStyle.Flow = UiFlow::Overlay;
	panelStyle.Padding = { 10, 10, 10, 10 };
	panelStyle.AlignItems = UiAlign::Center;
	const auto panel = Child(ui, ui.GetRoot(), panelStyle);
	const auto centered = Child(ui, panel, Box(40, 20));
	auto pinned = Box(30, 30);
	pinned.Absolute = true;
	pinned.AnchorMin = pinned.AnchorMax = { 1, 1 };
	pinned.Pivot = { 1, 1 };
	pinned.Offset = { -5, -5 };
	const auto corner = Child(ui, panel, pinned);
	UiStyle stretched;
	stretched.Absolute = true;
	stretched.AnchorMin = { 0, 0.5f };
	stretched.AnchorMax = { 1, 0.5f };
	stretched.Margin = { 4, 0, 6, 0 };
	stretched.Height = UiLength::Pixels(8);
	stretched.Pivot = { 0, 0.5f };
	const auto bar = Child(ui, panel, stretched);
	ui.Layout({ 500, 500 });
	SWIM_CHECK_NEAR(ui.GetBounds(centered).X, 10.0f + 70.0f, 1e-3f);
	SWIM_CHECK_NEAR(ui.GetBounds(centered).Y, 10.0f + 30.0f, 1e-3f);
	SWIM_CHECK_NEAR(ui.GetBounds(corner).X, 10.0f + 180.0f - 5.0f - 30.0f, 1e-3f);
	SWIM_CHECK_NEAR(ui.GetBounds(corner).Y, 10.0f + 80.0f - 5.0f - 30.0f, 1e-3f);
	SWIM_CHECK_NEAR(ui.GetBounds(bar).X, 14.0f, 1e-3f);
	SWIM_CHECK_NEAR(ui.GetBounds(bar).Width, 170.0f, 1e-3f);
	SWIM_CHECK_NEAR(ui.GetBounds(bar).Y, 10.0f + 40.0f - 4.0f, 1e-3f);

	UiStyle wide;
	wide.Width = UiLength::Pixels(120);
	wide.AspectRatio = 2.0f;
	const auto image = Child(ui, ui.GetRoot(), wide);
	UiStyle tall;
	tall.Height = UiLength::Pixels(30);
	tall.AspectRatio = 0.5f;
	const auto portrait = Child(ui, ui.GetRoot(), tall);
	ui.Layout({ 500, 500 });
	SWIM_CHECK_NEAR(ui.GetBounds(image).Height, 60.0f, 1e-3f);
	SWIM_CHECK_NEAR(ui.GetBounds(portrait).Width, 15.0f, 1e-3f);

	auto invalid = Box(10, 10);
	invalid.AnchorMin = { 1.5f, 0 };
	SWIM_CHECK_THROWS(ui.SetStyle(image, invalid), std::invalid_argument);
	invalid = Box(10, 10);
	invalid.Grow = -1;
	SWIM_CHECK_THROWS(ui.SetStyle(image, invalid), std::invalid_argument);
	invalid = Box(10, 10);
	invalid.Justify = static_cast<UiJustify>(99);
	SWIM_CHECK_THROWS(ui.SetStyle(image, invalid), std::invalid_argument);
}

SWIM_TEST("UI.Layout", "MeasureAndPaintCachesSkipUnchangedNodes")
{
	UiDocument ui;
	auto rootStyle = ui.GetStyle(ui.GetRoot());
	rootStyle.Flow = UiFlow::Row;
	ui.SetStyle(ui.GetRoot(), rootStyle);
	std::vector<UiNodeId> columns;
	std::vector<UiNodeId> leaves;
	for (int c = 0; c < 4; ++c)
	{
		columns.push_back(ui.Create(ui.GetRoot()));
		for (int i = 0; i < 5; ++i)
		{
			auto style = Box(20, 10);
			style.Background = { 1, 1, 1, 1 };
			leaves.push_back(Child(ui, columns.back(), style));
		}
	}
	Swim::Text::GlyphAtlas atlas;
	ui.Layout({ 400, 400 });
	SWIM_CHECK_EQUAL(ui.GetMeasuredNodeCount(), 25u); // Root, 4 columns and 20 leaves.
	SWIM_CHECK_EQUAL(ui.Paint(atlas).size(), 20u);
	SWIM_CHECK_EQUAL(ui.GetRepaintedNodeCount(), 25u);

	// A size change remeasures the leaf and its ancestors only; its column grows and
	// its later siblings move, so those four nodes repaint.
	auto taller = Box(20, 25);
	taller.Background = { 1, 1, 1, 1 };
	ui.SetStyle(leaves[7], taller);
	ui.Layout({ 400, 400 });
	SWIM_CHECK_EQUAL(ui.GetMeasuredNodeCount(), 3u);
	SWIM_CHECK_NEAR(ui.GetBounds(leaves[7]).Height, 25.0f, 1e-5f);
	ui.Paint(atlas);
	SWIM_CHECK_EQUAL(ui.GetRepaintedNodeCount(), 4u);

	// A color change is paint-only: no layout pass, one node repainted.
	const auto revision = ui.GetLayoutRevision();
	auto recolored = Box(20, 10);
	recolored.Background = { 1, 0, 0, 1 };
	ui.SetStyle(leaves[3], recolored);
	ui.Layout({ 400, 400 });
	SWIM_CHECK_EQUAL(ui.GetLayoutRevision(), revision);
	const auto& paint = ui.Paint(atlas);
	SWIM_CHECK_EQUAL(ui.GetRepaintedNodeCount(), 1u);
	SWIM_CHECK_NEAR(paint[3].Color.G, 0.0f, 1e-6f);

	// A taller leaf pushes its later siblings: those repaint, the other columns do not.
	ui.SetStyle(leaves[0], taller);
	ui.Layout({ 400, 400 });
	ui.Paint(atlas);
	SWIM_CHECK_EQUAL(ui.GetRepaintedNodeCount(), 6u); // Column 0 + its 5 leaves.
	// Another atlas repaints everything.
	Swim::Text::GlyphAtlas other;
	ui.Paint(other);
	SWIM_CHECK_EQUAL(ui.GetRepaintedNodeCount(), 25u);
}

SWIM_TEST("UI.Text", "WrapsAlignsAndFallsBackInsideNodes")
{
	const auto chain = Swim::Testing::LoadTextFontChain();
	const auto& primary = chain->GetPrimary();
	UiDocument ui;
	auto panelStyle = Box(160, 400);
	panelStyle.Padding = { 5, 5, 5, 5 };
	const auto panel = Child(ui, ui.GetRoot(), panelStyle);
	UiStyle labelStyle;
	labelStyle.TextWrap = Swim::Text::TextWrap::Word;
	labelStyle.TextAlign = Swim::Text::TextAlign::Center;
	const auto label = Child(ui, panel, labelStyle);
	ui.SetText(label, chain, "the quick brown fox jumps over the lazy dog", 20);
	ui.Layout({ 800, 800 });
	const auto bounds = ui.GetBounds(label);
	SWIM_CHECK(bounds.Width <= 150.0f + 1e-3f); // Auto width wraps inside the parent's content box.
	const auto* layout = ui.GetTextLayout(label);
	SWIM_REQUIRE(layout != nullptr);
	SWIM_CHECK(layout->GetLines().size() >= 3u);
	SWIM_CHECK_NEAR(bounds.Height, layout->GetHeight(), 1e-3f);
	Swim::Text::GlyphAtlas atlas;
	const auto& quads = ui.Paint(atlas);
	for (const auto& quad : quads)
	{
		SWIM_CHECK(quad.Kind == UiPaintKind::Glyph);
		SWIM_CHECK(quad.Bounds.X >= bounds.X - 4.0f);
		SWIM_CHECK(quad.Bounds.X + quad.Bounds.Width <= bounds.X + bounds.Width + 4.0f);
	}

	// Greek and Hebrew come from the fallback face; the atlas keys glyphs per face.
	const std::string mixed = "Hi \xCE\xA9\xCE\xBC\xCE\xAD\xCE\xB3\xCE\xB1 \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D";
	ui.SetText(label, chain, mixed, 20);
	ui.Layout({ 800, 800 });
	SWIM_CHECK_EQUAL(ui.GetTextLayout(label)->GetMissingGlyphs(), 0u);
	const auto glyphs = ui.Paint(atlas).size();
	SWIM_CHECK_EQUAL(glyphs, 11u); // H, i, 5 Greek, 4 Hebrew; spaces have no quads.

	// An RTL paragraph starts at the right edge of a fixed-width node.
	auto fixed = Box(300, 40);
	const auto rtl = Child(ui, ui.GetRoot(), fixed);
	ui.SetText(rtl, chain, "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85", 20);
	ui.Layout({ 800, 800 });
	const auto& line = ui.GetTextLayout(rtl)->GetLines()[0];
	SWIM_CHECK_NEAR(line.X + line.Width, 300.0f, 1e-3f);
	SWIM_CHECK_NEAR(ui.GetBounds(rtl).Width, 300.0f, 1e-5f);
	(void)primary;
}

SWIM_TEST("UI.Paint", "RoundedBordersImagesAndNineSlices")
{
	UiDocument ui;
	auto boxStyle = Box(100, 40);
	boxStyle.CornerRadius = 50.0f;
	boxStyle.BorderWidth = 2.0f;
	boxStyle.BorderColor = { 1, 1, 1, 0.5f };
	Child(ui, ui.GetRoot(), boxStyle); // Transparent fill, visible border.
	auto frameStyle = Box(100, 60);
	frameStyle.Padding = { 10, 10, 10, 10 };
	const auto frame = Child(ui, ui.GetRoot(), frameStyle);
	UiImage image;
	image.Texture = 7;
	image.Sampler = 3;
	image.Size = { 32, 32 };
	image.Slice = { 8, 8, 8, 8 };
	image.SliceUv = { 0.25f, 0.25f, 0.25f, 0.25f };
	image.Tint = { 1, 1, 1, 0.5f };
	ui.SetImage(frame, image);
	ui.Layout({ 400, 400 });
	Swim::Text::GlyphAtlas atlas;
	const auto& paint = ui.Paint(atlas);
	SWIM_REQUIRE_EQUAL(paint.size(), 10u);
	SWIM_CHECK(paint[0].Kind == UiPaintKind::Solid);
	SWIM_CHECK_NEAR(paint[0].CornerRadius, 20.0f, 1e-5f); // Clamped to half the shorter side.
	SWIM_CHECK_NEAR(paint[0].BorderColor.R, 0.5f, 1e-6f); // Premultiplied.
	for (std::size_t i = 1; i < 10; ++i)
	{
		SWIM_CHECK(paint[i].Kind == UiPaintKind::Image);
		SWIM_CHECK_EQUAL(paint[i].Texture, 7u);
		SWIM_CHECK_EQUAL(paint[i].Sampler, 3u);
		SWIM_CHECK_NEAR(paint[i].Color.A, 0.5f, 1e-6f);
	}
	// Corners keep 8 units; the centre stretches over the 80 x 40 content box.
	SWIM_CHECK_NEAR(paint[1].Bounds.Width, 8.0f, 1e-5f);
	SWIM_CHECK_NEAR(paint[5].Bounds.Width, 64.0f, 1e-5f);
	SWIM_CHECK_NEAR(paint[5].Bounds.Height, 24.0f, 1e-5f);
	SWIM_CHECK_NEAR(paint[5].Uv.X, 0.25f, 1e-6f);
	SWIM_CHECK_NEAR(paint[5].Uv.Width, 0.5f, 1e-6f);
	SWIM_CHECK_NEAR(paint[9].Bounds.X + paint[9].Bounds.Width, 90.0f, 1e-5f);

	// Contain keeps the aspect ratio, centered; the image sizes an Auto node.
	image.Slice = {};
	image.Fit = UiImageFit::Contain;
	image.Size = { 40, 20 };
	ui.SetImage(frame, image);
	const auto natural = Child(ui, ui.GetRoot(), UiStyle{});
	ui.SetImage(natural, image);
	ui.Layout({ 400, 400 });
	const auto& fitted = ui.Paint(atlas);
	SWIM_REQUIRE_EQUAL(fitted.size(), 3u);
	SWIM_CHECK_NEAR(fitted[1].Bounds.Width, 80.0f, 1e-5f);
	SWIM_CHECK_NEAR(fitted[1].Bounds.Height, 40.0f, 1e-5f);
	SWIM_CHECK_NEAR(ui.GetBounds(natural).Width, 40.0f, 1e-5f);
	ui.ClearImage(natural);
	ui.Layout({ 400, 400 });
	SWIM_CHECK_NEAR(ui.GetBounds(natural).Width, 0.0f, 1e-5f);
	image.SliceUv.Left = 2.0f;
	SWIM_CHECK_THROWS(ui.SetImage(frame, image), std::invalid_argument);
}
