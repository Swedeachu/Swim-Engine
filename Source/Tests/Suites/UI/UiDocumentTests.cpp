#include "Engine/Systems/UI/UiDocument.h"
#include "Tests/Fixtures/TextFontFixture.h"
#include "Tests/Framework/Test.h"

#include <algorithm>
#include <limits>
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

	std::size_t Count(const std::vector<UiEvent>& events, UiEventKind kind, UiNodeId node)
	{
		return std::count_if(events.begin(), events.end(),
			[=](const auto& event)
			{
				return event.Kind == kind && event.Node == node;
			});
	}
} // namespace

SWIM_TEST("UI.Layout", "MeasuresStackPaddingMarginsAndDpiInLogicalUnits")
{
	UiDocument ui;
	auto root = ui.GetStyle(ui.GetRoot());
	root.Padding = { 10, 10, 10, 10 };
	root.Flow = UiFlow::Row;
	root.Gap = 5;
	ui.SetStyle(ui.GetRoot(), root);
	const auto a = ui.Create(ui.GetRoot());
	const auto b = ui.Create(ui.GetRoot());
	auto aStyle = Box(40, 20);
	aStyle.Margin = { 2, 3, 4, 5 };
	aStyle.HitTest = true;
	ui.SetStyle(a, aStyle);
	auto bStyle = Box(10, 30);
	bStyle.Width = UiLength::Percent(0.5f);
	ui.SetStyle(b, bStyle);
	SWIM_CHECK_THROWS(ui.GetBounds(a), std::logic_error);
	ui.Layout({ 400, 200 }, 2);
	SWIM_CHECK_NEAR(ui.GetBounds(a).X, 12, 1e-5f);
	SWIM_CHECK_NEAR(ui.GetBounds(a).Y, 13, 1e-5f);
	SWIM_CHECK_NEAR(ui.GetBounds(b).X, 61, 1e-5f);
	SWIM_CHECK_NEAR(ui.GetBounds(b).Width, 90, 1e-5f);
	SWIM_CHECK(ui.HitTest({ 26, 28 }) == a);
	const auto revision = ui.GetLayoutRevision();
	ui.Layout({ 400, 200 }, 2);
	SWIM_CHECK_EQUAL(ui.GetLayoutRevision(), revision);
	ui.Layout({ 400, 200 }, 1);
	SWIM_CHECK_EQUAL(ui.GetLayoutRevision(), revision + 1);
	SWIM_CHECK_NEAR(ui.GetBounds(b).Width, 190, 1e-5f);
}

SWIM_TEST("UI.Layout", "IntrinsicSizeMinMaxAndAbsoluteChildrenHaveDefinedSizing")
{
	UiDocument ui;
	const auto panel = ui.Create(ui.GetRoot());
	UiStyle style;
	style.Padding = { 2, 3, 4, 5 };
	style.Gap = 7;
	style.MinSize.X = 60;
	ui.SetStyle(panel, style);
	const auto first = ui.Create(panel);
	const auto second = ui.Create(panel);
	const auto overlay = ui.Create(panel);
	ui.SetStyle(first, Box(20, 10));
	ui.SetStyle(second, Box(30, 15));
	auto absolute = Box(200, 300);
	absolute.Absolute = true;
	absolute.Offset = { 11, 13 };
	ui.SetStyle(overlay, absolute);
	ui.Layout({ 500, 500 });
	SWIM_CHECK_NEAR(ui.GetBounds(panel).Width, 60, 1e-5f);
	SWIM_CHECK_NEAR(ui.GetBounds(panel).Height, 40, 1e-5f); // 3 + 10 + 7 + 15 + 5.
	SWIM_CHECK_NEAR(ui.GetBounds(second).Y, 20, 1e-5f);
	SWIM_CHECK_NEAR(ui.GetBounds(overlay).X, 13, 1e-5f);
	SWIM_CHECK_NEAR(ui.GetBounds(overlay).Y, 16, 1e-5f);
	style.MaxSize.Y = 25;
	ui.SetStyle(panel, style);
	ui.Layout({ 500, 500 });
	SWIM_CHECK_NEAR(ui.GetBounds(panel).Height, 25, 1e-5f);
}

SWIM_TEST("UI.Layout", "PercentChildrenResolveAgainstFinalIntrinsicParentSize")
{
	UiDocument ui;
	const auto panel = ui.Create(ui.GetRoot());
	const auto fixed = ui.Create(panel);
	const auto relative = ui.Create(panel);
	const auto nested = ui.Create(relative);
	ui.SetStyle(fixed, Box(100, 20));
	auto percent = Box(0, 10);
	percent.Width = UiLength::Percent(0.5f);
	ui.SetStyle(relative, percent);
	ui.SetStyle(nested, percent);
	ui.Layout({ 500, 500 });
	SWIM_CHECK_NEAR(ui.GetBounds(panel).Width, 100, 1e-5f);
	SWIM_CHECK_NEAR(ui.GetBounds(relative).Width, 50, 1e-5f);
	SWIM_CHECK_NEAR(ui.GetBounds(nested).Width, 25, 1e-5f);
}

SWIM_TEST("UI.Layout", "ScrollingClampsAndClipsPaintAndHitTesting")
{
	UiDocument ui;
	const auto panel = ui.Create(ui.GetRoot());
	auto panelStyle = Box(60, 40);
	panelStyle.Clip = true;
	panelStyle.Padding = { 5, 5, 5, 5 };
	ui.SetStyle(panel, panelStyle);
	const auto child = ui.Create(panel);
	auto childStyle = Box(50, 100);
	childStyle.HitTest = true;
	childStyle.Background = { 0.8f, 0.4f, 0.2f, 0.5f };
	ui.SetStyle(child, childStyle);
	ui.SetScroll(panel, { 500, 500 });
	ui.Layout({ 100, 100 });
	SWIM_CHECK_NEAR(ui.GetScroll(panel).X, 0, 1e-5f);
	SWIM_CHECK_NEAR(ui.GetScroll(panel).Y, 70, 1e-5f);
	SWIM_CHECK_NEAR(ui.GetBounds(child).Y, -65, 1e-5f);
	SWIM_CHECK(ui.HitTest({ 10, 10 }) == child);
	SWIM_CHECK(!ui.HitTest({ 10, 36 })); // Parent's bottom padding is not interactive content.
	Swim::Text::GlyphAtlas atlas;
	const auto& paint = ui.Paint(atlas);
	SWIM_REQUIRE_EQUAL(paint.size(), 1u);
	SWIM_CHECK_NEAR(paint[0].Clip.Y, 5, 1e-5f);
	SWIM_CHECK_NEAR(paint[0].Clip.Height, 30, 1e-5f);
	SWIM_CHECK_NEAR(paint[0].Color.R, 0.4f, 1e-5f);
	SWIM_CHECK_NEAR(paint[0].Color.A, 0.5f, 1e-5f);
}

SWIM_TEST("UI.Tree", "RejectsCyclesStaleHandlesAndCrossDocumentNodes")
{
	UiDocument ui;
	const auto a = ui.Create(ui.GetRoot());
	const auto b = ui.Create(a);
	const auto c = ui.Create(b);
	SWIM_CHECK_THROWS(ui.Reparent(a, c), std::invalid_argument);
	SWIM_CHECK_THROWS(ui.Reparent(ui.GetRoot(), a), std::invalid_argument);
	UiDocument other;
	SWIM_CHECK_THROWS(ui.Create(other.GetRoot()), std::out_of_range);
	ui.Reparent(c, ui.GetRoot());
	SWIM_CHECK(ui.Remove(a));
	SWIM_CHECK(!ui.Contains(a));
	SWIM_CHECK(!ui.Contains(b));
	SWIM_CHECK(ui.Contains(c));
	SWIM_CHECK(!ui.Remove(ui.GetRoot()));
	SWIM_CHECK(!ui.Remove(a));
	SWIM_CHECK_THROWS(ui.SetStyle(a, {}), std::out_of_range);
	const auto replacement = ui.Create(ui.GetRoot());
	SWIM_CHECK(replacement != a && replacement != b);
	ui.Layout({ 100, 100 });
}

SWIM_TEST("UI.Input", "UsesPaintOrderPointerCaptureAndKeyboardFocus")
{
	UiDocument ui;
	auto root = ui.GetStyle(ui.GetRoot());
	root.Flow = UiFlow::Overlay;
	ui.SetStyle(ui.GetRoot(), root);
	auto style = Box(80, 40);
	style.HitTest = true;
	style.Focusable = true;
	const auto lower = ui.Create(ui.GetRoot());
	const auto upper = ui.Create(ui.GetRoot());
	ui.SetStyle(lower, style);
	ui.SetStyle(upper, style);
	ui.Layout({ 100, 100 });
	SWIM_CHECK(ui.HitTest({ 10, 10 }) == upper);
	ui.PointerDown({ 10, 10 });
	SWIM_CHECK(ui.GetFocus() == upper);
	ui.PointerUp({ 90, 90 });
	auto events = ui.DrainEvents();
	SWIM_CHECK_EQUAL(Count(events, UiEventKind::Press, upper), 1u);
	SWIM_CHECK_EQUAL(Count(events, UiEventKind::Release, upper), 1u);
	SWIM_CHECK_EQUAL(Count(events, UiEventKind::Click, upper), 0u);
	ui.PointerDown({ 10, 10 });
	ui.PointerUp({ 10, 10 });
	SWIM_CHECK_EQUAL(Count(ui.DrainEvents(), UiEventKind::Click, upper), 1u);
	ui.FocusNext();
	SWIM_CHECK(ui.GetFocus() == lower);
	ui.FocusNext(true);
	SWIM_CHECK(ui.GetFocus() == upper);
	ui.ActivateFocused();
	SWIM_CHECK_EQUAL(Count(ui.DrainEvents(), UiEventKind::Click, upper), 1u);
	ui.PointerDown({ 10, 10 });
	ui.CancelPointer();
	ui.PointerUp({ 10, 10 });
	events = ui.DrainEvents();
	SWIM_CHECK_EQUAL(Count(events, UiEventKind::Cancel, upper), 1u);
	SWIM_CHECK_EQUAL(Count(events, UiEventKind::Click, upper), 0u);
}

SWIM_TEST("UI.Input", "AncestorChangesAndRemovalCancelFocusAndCapture")
{
	UiDocument ui;
	const auto parent = ui.Create(ui.GetRoot());
	const auto button = ui.Create(parent);
	auto style = Box(50, 30);
	style.HitTest = true;
	style.Focusable = true;
	ui.SetStyle(button, style);
	ui.Layout({ 100, 100 });
	ui.PointerDown({ 10, 10 });
	ui.DrainEvents();
	auto parentStyle = ui.GetStyle(parent);
	parentStyle.Enabled = false;
	ui.SetStyle(parent, parentStyle);
	SWIM_CHECK(!ui.GetFocus());
	auto events = ui.DrainEvents();
	SWIM_CHECK_EQUAL(Count(events, UiEventKind::Blur, button), 1u);
	SWIM_CHECK_EQUAL(Count(events, UiEventKind::Cancel, button), 1u);
	SWIM_CHECK_THROWS(ui.Focus(button), std::invalid_argument);
	ui.Layout({ 100, 100 });
	SWIM_CHECK(!ui.HitTest({ 10, 10 }));
	parentStyle.Enabled = true;
	ui.SetStyle(parent, parentStyle);
	ui.Layout({ 100, 100 });
	ui.PointerDown({ 10, 10 });
	ui.DrainEvents();
	ui.Remove(parent);
	events = ui.DrainEvents();
	SWIM_CHECK_EQUAL(Count(events, UiEventKind::Cancel, button), 1u);
	SWIM_CHECK_EQUAL(Count(events, UiEventKind::Blur, button), 1u);
	ui.Layout({ 100, 100 });
	ui.PointerUp({ 10, 10 });
	SWIM_CHECK_EQUAL(Count(ui.DrainEvents(), UiEventKind::Click, button), 0u);
}

SWIM_TEST("UI.Text", "MeasuresHardLinesAndProducesCachedGlyphQuads")
{
	const auto font = Swim::Testing::LoadTextFontFixture();
	UiDocument ui;
	const auto label = ui.Create(ui.GetRoot());
	ui.SetText(label, font, "AV\r\nffi\n", 24);
	ui.Layout({ 400, 300 });
	SWIM_CHECK_NEAR(ui.GetBounds(label).Width, font->Shape("AV", 24).AdvanceX, 1e-5f);
	SWIM_CHECK_NEAR(ui.GetBounds(label).Height, font->GetMetrics(24).LineHeight * 3, 1e-5f);
	Swim::Text::GlyphAtlas atlas;
	const auto first = ui.Paint(atlas);
	SWIM_REQUIRE_EQUAL(first.size(), 3u); // A, V and the ffi ligature, with an empty final line.
	SWIM_CHECK(first[0].Kind == UiPaintKind::Glyph);
	SWIM_CHECK(first[2].Bounds.Y > first[0].Bounds.Y);
	SWIM_CHECK(first[0].Uv.Width > 0.0f);
	const auto revision = atlas.GetPage(0).Revision;
	ui.Paint(atlas);
	SWIM_CHECK_EQUAL(atlas.GetPage(0).Revision, revision);
	SWIM_CHECK_EQUAL(atlas.GetGlyphCount(), 3u);
	ui.SetText(label, font, "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85", 24);
	ui.Layout({ 400, 300 });
	SWIM_CHECK_EQUAL(ui.Paint(atlas).size(), 3u);
	ui.SetText(label, std::shared_ptr<const Swim::Text::FontFace>{}, {}, 24);
	ui.Layout({ 400, 300 });
	SWIM_CHECK(ui.Paint(atlas).empty());
	SWIM_CHECK_EQUAL(ui.GetBounds(label).Height, 0.0f);
}

SWIM_TEST("UI.Layout", "HiddenNodesClearBoundsAndInvalidInputLeavesTreeUsable")
{
	UiDocument ui;
	const auto child = ui.Create(ui.GetRoot());
	auto style = Box(30, 20);
	ui.SetStyle(child, style);
	ui.Layout({ 100, 100 });
	style.Visible = false;
	ui.SetStyle(child, style);
	ui.Layout({ 100, 100 });
	SWIM_CHECK_EQUAL(ui.GetBounds(child).Width, 0.0f);
	style.Visible = true;
	style.Gap = std::numeric_limits<float>::quiet_NaN();
	SWIM_CHECK_THROWS(ui.SetStyle(child, style), std::invalid_argument);
	SWIM_CHECK(!ui.GetStyle(child).Visible);
	SWIM_CHECK_THROWS(ui.Layout({ 100, 100 }, 0), std::invalid_argument);
	SWIM_CHECK_THROWS(ui.SetScroll(child, { 0, std::numeric_limits<float>::infinity() }), std::invalid_argument);
	SWIM_CHECK(!ui.HitTest({ std::numeric_limits<float>::quiet_NaN(), 0 }));
	ui.Layout({ 0, 0 });
	SWIM_CHECK(!ui.HitTest({ 0, 0 }));
}
