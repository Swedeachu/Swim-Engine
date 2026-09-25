#include "Engine/Systems/UI/UiWidgets.h"
#include "Tests/Fixtures/TextFontFixture.h"
#include "Tests/Framework/Test.h"

#include <algorithm>
#include <stdexcept>

using namespace Swim::UI;

namespace
{
	std::shared_ptr<UiTheme> FontTheme()
	{
		auto theme = std::make_shared<UiTheme>();
		theme->Fonts = Swim::Testing::LoadTextFontChain();
		return theme;
	}

	bool SameColor(const UiColor& a, const UiColor& b)
	{
		return std::abs(a.R - b.R) < 1e-5f && std::abs(a.G - b.G) < 1e-5f && std::abs(a.B - b.B) < 1e-5f && std::abs(a.A - b.A) < 1e-5f;
	}

	UiPoint Center(const UiRect& rect)
	{
		return { rect.X + rect.Width * 0.5f, rect.Y + rect.Height * 0.5f };
	}

	std::size_t QuadsOf(const std::vector<UiPaintQuad>& paint, UiNodeId node)
	{
		return std::count_if(paint.begin(), paint.end(),
			[&](const UiPaintQuad& quad)
			{
				return quad.Node == node;
			});
	}
} // namespace

SWIM_TEST("UI.Widgets", "ButtonsFollowHoverPressAndFocusThroughTheThemeWithoutRelayout")
{
	UiDocument ui;
	const auto theme = FontTheme();
	ui.SetTheme(theme);
	const auto& palette = theme->Palette;
	UiStyle column;
	column.Gap = 4;
	ui.SetStyle(ui.GetRoot(), column);
	const auto button = CreateButton(ui, ui.GetRoot(), "Play");
	const auto label = CreateLabel(ui, ui.GetRoot(), "Score: 0");
	const auto field = CreateTextField(ui, ui.GetRoot());
	UiImage icon;
	icon.Texture = 5;
	icon.Size = { 24, 24 };
	const auto image = CreateImage(ui, ui.GetRoot(), icon);
	SWIM_CHECK(ui.IsEditable(field));
	SWIM_CHECK(!ui.GetStyle(label).HitTest);
	SWIM_CHECK(ui.GetControl(button).Kind == UiControlKind::Button);
	SWIM_CHECK(ui.GetThemeClass(button) == UiThemeClass::Button);
	ui.Layout({ 400, 400 });
	Swim::Text::GlyphAtlas atlas;
	ui.Paint(atlas);
	const auto bounds = ui.GetBounds(button);
	SWIM_CHECK(bounds.Width > 24.0f + theme->Metrics.ButtonPadding.Left + theme->Metrics.ButtonPadding.Right);
	SWIM_CHECK(bounds.Height >= theme->Metrics.ControlHeight);
	SWIM_CHECK_NEAR(ui.GetBounds(image).Width, 24.0f, 1e-5f);
	SWIM_CHECK(SameColor(ui.GetVisual(button).Background, palette.Surface));
	SWIM_CHECK(SameColor(ui.GetVisual(label).TextColor, palette.Text));
	const auto revision = ui.GetLayoutRevision();

	ui.PointerMove(Center(bounds));
	ui.Update(0.0f);
	SWIM_CHECK(HasState(ui.GetState(button), UiState::Hovered));
	SWIM_CHECK(SameColor(ui.GetVisual(button).Background, palette.SurfaceHover));
	ui.PointerDown(Center(bounds));
	ui.Update(0.0f);
	SWIM_CHECK(HasState(ui.GetState(button), UiState::Pressed | UiState::Focused));
	SWIM_CHECK(SameColor(ui.GetVisual(button).Background, palette.SurfacePressed));
	SWIM_CHECK_NEAR(ui.GetVisual(button).BorderWidth, theme->Metrics.FocusWidth, 1e-6f);
	ui.PointerUp({ 390, 390 });
	ui.Update(0.0f);
	SWIM_CHECK(SameColor(ui.GetVisual(button).Background, palette.Surface)); // Released outside.
	const auto events = ui.DrainEvents();
	SWIM_CHECK(std::none_of(events.begin(), events.end(),
		[](const UiEvent& event)
		{
			return event.Kind == UiEventKind::Click;
		}));
	ui.Layout({ 400, 400 });
	SWIM_CHECK_EQUAL(ui.GetLayoutRevision(), revision); // Visual states are paint-only.
	ui.Paint(atlas);
	SWIM_CHECK(ui.GetRepaintedNodeCount() >= 1u);
	ui.Focus({});
	ui.Update(0.0f);
	SWIM_CHECK_NEAR(ui.GetVisual(button).BorderWidth, 0.0f, 1e-6f);

	// Disabled nodes take the theme's disabled look (and no input).
	auto style = ui.GetStyle(button);
	style.Enabled = false;
	ui.SetStyle(button, style);
	ui.Update(0.0f);
	SWIM_CHECK(HasState(ui.GetState(button), UiState::Disabled));
	SWIM_CHECK_NEAR(ui.GetVisual(button).Opacity, theme->Metrics.DisabledOpacity, 1e-6f);
	ui.Layout({ 400, 400 });
	SWIM_CHECK(!ui.HitTest(Center(bounds)));
	const auto& paint = ui.Paint(atlas);
	const auto background = std::find_if(paint.begin(), paint.end(),
		[&](const UiPaintQuad& quad)
		{
			return quad.Node == button && quad.Kind == UiPaintKind::Solid;
		});
	SWIM_REQUIRE(background != paint.end());
	SWIM_CHECK_NEAR(background->Color.A, theme->Metrics.DisabledOpacity, 1e-5f); // Premultiplied and faded.
	SWIM_CHECK(ui.Remove(button));
}

SWIM_TEST("UI.Theme", "ThemeChangesRestyleEveryWidgetAndNodeRulesOverrideTheClass")
{
	UiDocument ui;
	auto theme = FontTheme();
	ui.SetTheme(theme);
	const auto button = CreateButton(ui, ui.GetRoot(), "OK");
	const auto slider = CreateSlider(ui, ui.GetRoot());
	const auto vertical = CreateSlider(ui, ui.GetRoot(), { .Orientation = UiOrientation::Vertical });
	ui.Layout({ 400, 400 });
	SWIM_CHECK_NEAR(ui.GetBounds(slider).Width, theme->Metrics.SliderLength, 1e-5f);
	SWIM_CHECK_NEAR(ui.GetBounds(vertical).Height, theme->Metrics.SliderLength, 1e-5f); // Axes swapped.
	SWIM_CHECK_NEAR(ui.GetBounds(vertical).Width, theme->Metrics.SliderThumb, 1e-5f);

	// A new theme: metrics, palette, fonts and a Customize hook reach every themed node.
	auto next = std::make_shared<UiTheme>(*theme);
	next->Metrics.SliderLength = 240.0f;
	next->Metrics.TextSize = 24.0f;
	next->Palette.Surface = { 0.5f, 0.1f, 0.1f, 1.0f };
	next->Customize = [](UiThemeClass themeClass, UiClassStyle& style)
	{
		if (themeClass == UiThemeClass::SliderThumb)
		{
			style.Style.CornerRadius = 2.0f; // Square thumbs.
		}
	};
	const auto buttonHeight = ui.GetBounds(button).Height;
	ui.SetTheme(next);
	SWIM_CHECK(!ui.IsLayoutCurrent());
	ui.Layout({ 400, 400 });
	ui.Update(0.0f);
	SWIM_CHECK_NEAR(ui.GetBounds(slider).Width, 240.0f, 1e-5f);
	SWIM_CHECK_NEAR(ui.GetBounds(vertical).Height, 240.0f, 1e-5f);
	SWIM_CHECK(ui.GetBounds(button).Height > buttonHeight); // Larger themed text.
	SWIM_CHECK(SameColor(ui.GetVisual(button).Background, next->Palette.Surface));
	SWIM_CHECK_NEAR(ui.GetVisual(ui.GetControl(slider).Parts.Thumb).CornerRadius, 2.0f, 1e-6f);

	// Per-node rules apply after the class's rules; the node's own style stays the base.
	UiVisual red;
	red.Background = UiColor{ 1, 0, 0, 1 };
	ui.SetStateRules(button, { { UiState::Hovered, UiState::None, red } });
	ui.PointerMove(Center(ui.GetBounds(button)));
	ui.Update(0.0f);
	SWIM_CHECK(SameColor(ui.GetVisual(button).Background, { 1, 0, 0, 1 }));
	ui.PointerMove({ 399, 399 });
	ui.Update(0.0f);
	SWIM_CHECK(SameColor(ui.GetVisual(button).Background, next->Palette.Surface));

	// Unthemed nodes keep their style; Layout-less theming keeps a node's own sizes.
	const auto custom = CreateButton(ui, ui.GetRoot(), "Custom");
	auto style = ui.GetStyle(custom);
	style.Width = UiLength::Pixels(123);
	ui.SetStyle(custom, style);
	ui.SetThemeClass(custom, UiThemeClass::Button, UiThemeApply::Paint | UiThemeApply::Text);
	ui.SetTheme(theme);
	ui.Layout({ 400, 400 });
	SWIM_CHECK_NEAR(ui.GetBounds(custom).Width, 123.0f, 1e-5f);

	// Invalid themes and rules are rejected before anything changes.
	auto broken = std::make_shared<UiTheme>(*theme);
	broken->Customize = [](UiThemeClass, UiClassStyle& style)
	{
		style.Style.Opacity = 2.0f;
	};
	SWIM_CHECK_THROWS(ui.SetTheme(broken), std::invalid_argument);
	SWIM_CHECK(ui.GetTheme() == theme);
	SWIM_CHECK_THROWS(ui.SetTheme(nullptr), std::invalid_argument);
	UiVisual invalid;
	invalid.Opacity = -1.0f;
	SWIM_CHECK_THROWS(ui.SetStateRules(button, { { UiState::None, UiState::None, invalid } }), std::invalid_argument);
	SWIM_CHECK_THROWS(ui.SetThemeClass(button, UiThemeClass::Count), std::invalid_argument);
}

SWIM_TEST("UI.Theme", "TransitionsEaseStateChangesAndImageSkinsFollowStates")
{
	UiDocument ui;
	auto theme = FontTheme();
	theme->Metrics.TransitionSeconds = 0.2f;
	ui.SetTheme(theme);
	const auto button = CreateButton(ui, ui.GetRoot(), "Fade");
	ui.Layout({ 200, 100 });
	ui.Update(0.0f);
	const auto from = theme->Palette.Surface;
	const auto to = theme->Palette.SurfaceHover;
	ui.PointerMove(Center(ui.GetBounds(button)));
	SWIM_CHECK(ui.Update(0.1f)); // Half way: smoothstep(0.5) = 0.5.
	SWIM_CHECK(ui.IsAnimating());
	SWIM_CHECK_NEAR(ui.GetVisual(button).Background.R, (from.R + to.R) * 0.5f, 1e-5f);
	SWIM_CHECK(!ui.Update(0.2f));
	SWIM_CHECK(!ui.IsAnimating());
	SWIM_CHECK(SameColor(ui.GetVisual(button).Background, to));

	// Style changes snap even with a transition.
	auto style = ui.GetStyle(button);
	style.TransitionSeconds = 0.0f;
	ui.SetStyle(button, style);
	ui.PointerMove({ 199, 99 });
	ui.Update(0.0f);
	SWIM_CHECK(SameColor(ui.GetVisual(button).Background, from));

	// Image skins per state never change layout.
	UiImage normal;
	normal.Texture = 1;
	normal.Size = { 10, 10 };
	UiImage hovered = normal;
	hovered.Texture = 2;
	hovered.Tint = { 1, 1, 1, 0.5f };
	UiVisual skin;
	skin.Image = hovered;
	const auto icon = CreateImage(ui, ui.GetRoot(), normal);
	auto iconStyle = ui.GetStyle(icon);
	iconStyle.HitTest = true;
	ui.SetStyle(icon, iconStyle);
	ui.SetStateRules(icon, { { UiState::Hovered, UiState::None, skin } });
	ui.Layout({ 200, 100 });
	const auto iconBounds = ui.GetBounds(icon);
	const auto revision = ui.GetLayoutRevision();
	Swim::Text::GlyphAtlas atlas;
	ui.PointerMove(Center(iconBounds));
	const auto& paint = ui.Paint(atlas);
	const auto quad = std::find_if(paint.begin(), paint.end(),
		[&](const UiPaintQuad& q)
		{
			return q.Node == icon && q.Kind == UiPaintKind::Image;
		});
	SWIM_REQUIRE(quad != paint.end());
	SWIM_CHECK_EQUAL(quad->Texture, 2u);
	SWIM_CHECK_NEAR(quad->Color.A, 0.5f, 1e-6f);
	SWIM_CHECK_EQUAL(ui.GetLayoutRevision(), revision);
	const auto before = ui.GetPaintRevision();
	ui.Paint(atlas);
	SWIM_CHECK_EQUAL(ui.GetPaintRevision(), before); // Nothing changed.
	SWIM_CHECK_THROWS(ui.Update(-1.0f), std::invalid_argument);
}

SWIM_TEST("UI.Widgets", "OpacityMultipliesDownTheTreeAndRepaintsDescendants")
{
	UiDocument ui;
	const auto parent = ui.Create(ui.GetRoot());
	const auto child = ui.Create(parent);
	UiStyle box;
	box.Width = UiLength::Pixels(20);
	box.Height = UiLength::Pixels(20);
	box.Background = { 1, 1, 1, 1 };
	ui.SetStyle(child, box);
	ui.Layout({ 100, 100 });
	Swim::Text::GlyphAtlas atlas;
	ui.Paint(atlas);
	auto style = ui.GetStyle(parent);
	style.Opacity = 0.25f;
	ui.SetStyle(parent, style);
	SWIM_CHECK(ui.IsLayoutCurrent()); // Paint-only.
	const auto& paint = ui.Paint(atlas);
	SWIM_REQUIRE_EQUAL(QuadsOf(paint, child), 1u);
	SWIM_CHECK_NEAR(paint.back().Color.A, 0.25f, 1e-6f);
	SWIM_CHECK_NEAR(paint.back().Color.R, 0.25f, 1e-6f);
	style.Opacity = 0.0f;
	ui.SetStyle(parent, style);
	SWIM_CHECK_EQUAL(QuadsOf(ui.Paint(atlas), child), 0u);
}
