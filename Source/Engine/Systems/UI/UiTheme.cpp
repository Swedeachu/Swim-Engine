#include "Engine/Systems/UI/UiTheme.h"
#include "Engine/Systems/UI/Internal/UiDocumentImpl.h"

#include <stdexcept>

namespace Swim::UI
{
	namespace
	{
		UiColor WithAlpha(UiColor color, float alpha)
		{
			color.A = alpha;
			return color;
		}

		UiStateRule Rule(UiState when, UiVisual visual, UiState unless = UiState::None)
		{
			return { when, unless, std::move(visual) };
		}

		UiVisual Fill(UiColor background)
		{
			UiVisual visual;
			visual.Background = background;
			return visual;
		}

		UiVisual FocusRing(const UiPalette& p, const UiMetrics& m)
		{
			UiVisual visual;
			visual.BorderColor = p.Focus;
			visual.BorderWidth = m.FocusWidth;
			return visual;
		}

		UiVisual Faded(float opacity)
		{
			UiVisual visual;
			visual.Opacity = opacity;
			return visual;
		}

		UiLength Px(float value)
		{
			return UiLength::Pixels(value);
		}
	} // namespace

	UiClassStyle UiTheme::Class(UiThemeClass themeClass) const
	{
		const auto& p = Palette;
		const auto& m = Metrics;
		UiClassStyle c;
		auto& s = c.Style;
		s.TransitionSeconds = m.TransitionSeconds;
		s.TextColor = p.Text;
		s.SelectionColor = p.Selection;
		s.CaretColor = p.Caret;
		c.TextSize = m.TextSize;
		switch (themeClass)
		{
		case UiThemeClass::Panel:
			s.Background = p.Panel;
			s.CornerRadius = m.CornerRadius;
			s.Padding = { m.Spacing, m.Spacing, m.Spacing, m.Spacing };
			s.Gap = m.Spacing;
			break;
		case UiThemeClass::Label:
		{
			c.TextSize = m.LabelTextSize;
			UiVisual muted;
			muted.TextColor = p.TextMuted;
			c.Rules.push_back(Rule(UiState::Disabled, muted));
			break;
		}
		case UiThemeClass::Button:
			s.Padding = m.ButtonPadding;
			s.MinSize = { 0.0f, m.ControlHeight };
			s.Background = p.Surface;
			s.CornerRadius = m.CornerRadius;
			s.BorderColor = p.Focus;
			c.Rules = { Rule(UiState::Hovered, Fill(p.SurfaceHover)), Rule(UiState::Pressed, Fill(p.SurfacePressed)),
				Rule(UiState::Focused, FocusRing(p, m)), Rule(UiState::Disabled, Faded(m.DisabledOpacity)) };
			break;
		case UiThemeClass::TextField:
		{
			s.Padding = m.FieldPadding;
			s.MinSize = { 0.0f, m.ControlHeight };
			s.Background = p.Field;
			s.BorderWidth = m.BorderWidth;
			s.BorderColor = p.Border;
			s.CornerRadius = std::min(m.CornerRadius, 4.0f);
			UiVisual hover;
			hover.BorderColor = p.TextMuted;
			c.Rules = { Rule(UiState::Hovered, hover), Rule(UiState::Focused, FocusRing(p, m)),
				Rule(UiState::Disabled, Faded(m.DisabledOpacity)) };
			break;
		}
		case UiThemeClass::ScrollView:
			break;
		case UiThemeClass::ScrollBar:
			s.Height = Px(m.ScrollBarThickness);
			s.Background = WithAlpha(p.Track, 0.35f);
			s.CornerRadius = m.ScrollBarThickness * 0.5f;
			c.Rules = { Rule(UiState::Hovered, Fill(WithAlpha(p.Track, 0.6f))) };
			break;
		case UiThemeClass::ScrollThumb:
			s.Background = WithAlpha(p.TextMuted, 0.7f);
			s.CornerRadius = m.ScrollBarThickness * 0.5f;
			c.Rules = { Rule(UiState::Hovered, Fill(WithAlpha(p.Text, 0.8f))), Rule(UiState::Dragging, Fill(p.Accent)) };
			break;
		case UiThemeClass::Checkbox:
		case UiThemeClass::Toggle:
			s.Gap = m.Spacing * 0.75f;
			c.TextSize = m.LabelTextSize;
			c.Rules = { Rule(UiState::Disabled, Faded(m.DisabledOpacity)) };
			break;
		case UiThemeClass::CheckBox:
		{
			s.Width = Px(m.CheckboxSize);
			s.Height = Px(m.CheckboxSize);
			s.Background = p.Surface;
			s.BorderWidth = m.BorderWidth;
			s.BorderColor = p.Border;
			s.CornerRadius = std::min(m.CornerRadius, 4.0f);
			UiVisual on;
			on.Background = p.Accent;
			on.BorderColor = p.Accent;
			UiVisual onHover = on;
			onHover.Background = p.AccentHover;
			onHover.BorderColor = p.AccentHover;
			c.Rules = { Rule(UiState::Hovered, Fill(p.SurfaceHover)), Rule(UiState::Pressed, Fill(p.SurfacePressed)),
				Rule(UiState::Checked, on), Rule(UiState::Mixed, on), Rule(UiState::Checked | UiState::Hovered, onHover),
				Rule(UiState::Mixed | UiState::Hovered, onHover), Rule(UiState::Focused, FocusRing(p, m)) };
			break;
		}
		case UiThemeClass::CheckMark:
			s.Width = Px(m.CheckboxSize * 0.5f);
			s.Height = Px(m.CheckboxSize * 0.5f);
			s.Background = p.OnAccent;
			s.CornerRadius = 2.0f;
			s.Opacity = 0.0f;
			c.Rules = { Rule(UiState::Checked, Faded(1.0f)) };
			break;
		case UiThemeClass::CheckMixed:
			s.Width = Px(m.CheckboxSize * 0.55f);
			s.Height = Px(std::max(2.0f, m.CheckboxSize / 9.0f));
			s.Background = p.OnAccent;
			s.CornerRadius = 1.0f;
			s.Opacity = 0.0f;
			c.Rules = { Rule(UiState::Mixed, Faded(1.0f)) };
			break;
		case UiThemeClass::ToggleTrack:
		{
			s.Width = Px(m.ToggleWidth);
			s.Height = Px(m.ToggleHeight);
			s.Padding = { m.ToggleInset, m.ToggleInset, m.ToggleInset, m.ToggleInset };
			s.Background = p.Track;
			s.CornerRadius = m.ToggleHeight * 0.5f;
			c.Rules = { Rule(UiState::Hovered, Fill(p.SurfaceHover)), Rule(UiState::Checked, Fill(p.Accent)),
				Rule(UiState::Checked | UiState::Hovered, Fill(p.AccentHover)), Rule(UiState::Focused, FocusRing(p, m)) };
			break;
		}
		case UiThemeClass::ToggleKnob:
		{
			const float knob = std::max(0.0f, m.ToggleHeight - 2.0f * m.ToggleInset);
			s.Width = Px(knob);
			s.Height = Px(knob);
			s.Background = p.Text;
			s.CornerRadius = knob * 0.5f;
			c.Rules = { Rule(UiState::Checked, Fill(p.OnAccent)) };
			break;
		}
		case UiThemeClass::Slider:
			s.Width = Px(m.SliderLength);
			s.Height = Px(std::max(m.SliderThumb, m.SliderTrack));
			c.Rules = { Rule(UiState::Disabled, Faded(m.DisabledOpacity)) };
			break;
		case UiThemeClass::SliderTrack:
			s.Height = Px(m.SliderTrack);
			s.Background = p.Track;
			s.CornerRadius = m.SliderTrack * 0.5f;
			break;
		case UiThemeClass::SliderFill:
			s.Height = Px(m.SliderTrack);
			s.Background = p.Accent;
			s.CornerRadius = m.SliderTrack * 0.5f;
			c.Rules = { Rule(UiState::Hovered, Fill(p.AccentHover)) };
			break;
		case UiThemeClass::SliderThumb:
		{
			s.Width = Px(m.SliderThumb);
			s.Height = Px(m.SliderThumb);
			s.Background = p.OnAccent;
			s.BorderWidth = m.BorderWidth;
			s.BorderColor = p.Border;
			s.CornerRadius = m.SliderThumb * 0.5f;
			UiVisual hover;
			hover.BorderColor = p.Accent;
			UiVisual drag;
			drag.BorderColor = p.AccentPressed;
			drag.BorderWidth = std::max(m.BorderWidth, m.FocusWidth);
			c.Rules = { Rule(UiState::Hovered, hover), Rule(UiState::Focused, FocusRing(p, m)), Rule(UiState::Dragging, drag) };
			break;
		}
		case UiThemeClass::SliderTick:
			s.Width = Px(std::max(1.0f, m.SliderTrack * 0.5f));
			s.Height = Px(m.SliderThumb * 0.75f);
			s.Background = WithAlpha(p.TextMuted, 0.6f);
			s.CornerRadius = 1.0f;
			break;
		case UiThemeClass::SliderValue:
			s.MinSize = { m.LabelTextSize * 2.5f, 0.0f }; // Values of changing width do not shift the layout.
			c.TextSize = m.LabelTextSize;
			c.Rules = { Rule(UiState::Disabled,
							[&]
							{
								UiVisual v;
								v.TextColor = p.TextMuted;
								return v;
							}()),
				Rule(UiState::Focused, FocusRing(p, m)) }; // Editable values (UiSliderDesc::EditableValue).
			break;
		case UiThemeClass::ScrollButton:
			s.Background = WithAlpha(p.Track, 0.5f);
			s.CornerRadius = m.ScrollBarThickness * 0.25f;
			c.Rules = { Rule(UiState::Hovered, Fill(WithAlpha(p.Track, 0.85f))), Rule(UiState::Pressed, Fill(p.Accent)) };
			break;
		case UiThemeClass::Popup:
			s.Background = p.Popup;
			s.BorderWidth = m.BorderWidth;
			s.BorderColor = p.Border;
			s.CornerRadius = m.CornerRadius;
			s.Padding = { m.PopupPadding, m.PopupPadding, m.PopupPadding, m.PopupPadding };
			s.MaxSize = { Internal::MaxLogical, std::max(m.PopupMaxHeight, 1.0f) };
			break;
		case UiThemeClass::MenuItem:
		{
			// Menu entries (buttons) and dropdown/list options. Focused marks the keyboard
			// item (or an open dropdown's highlight); Checked the selected option.
			s.Padding = m.MenuItemPadding;
			s.MinSize = { 0.0f, m.ControlHeight };
			s.CornerRadius = std::min(m.CornerRadius, 4.0f);
			UiVisual selected;
			selected.Background = WithAlpha(p.Accent, 0.35f);
			UiVisual selectedFocus;
			selectedFocus.Background = p.Accent;
			selectedFocus.TextColor = p.OnAccent;
			c.TextSize = m.LabelTextSize;
			c.Rules = { Rule(UiState::Hovered, Fill(p.SurfaceHover)), Rule(UiState::Checked, selected),
				Rule(UiState::Focused, Fill(p.SurfaceHover)), Rule(UiState::Checked | UiState::Focused, selectedFocus),
				Rule(UiState::Pressed, Fill(p.SurfacePressed)), Rule(UiState::Disabled, Faded(m.DisabledOpacity)) };
			break;
		}
		case UiThemeClass::MenuSeparator:
			s.Height = Px(std::max(1.0f, m.BorderWidth));
			s.Background = WithAlpha(p.Border, 0.8f);
			break;
		case UiThemeClass::ListView:
			s.Background = p.Field;
			s.BorderWidth = m.BorderWidth;
			s.BorderColor = p.Border;
			s.CornerRadius = std::min(m.CornerRadius, 4.0f);
			s.Padding = { m.BorderWidth, m.BorderWidth, m.BorderWidth, m.BorderWidth };
			c.Rules = { Rule(UiState::Focused, FocusRing(p, m)), Rule(UiState::Disabled, Faded(m.DisabledOpacity)) };
			break;
		case UiThemeClass::Dropdown:
			s.Padding = m.FieldPadding;
			s.MinSize = { 0.0f, m.ControlHeight };
			s.Gap = m.Spacing;
			s.Background = p.Surface;
			s.BorderWidth = m.BorderWidth;
			s.BorderColor = p.Border;
			s.CornerRadius = std::min(m.CornerRadius, 4.0f);
			c.TextSize = m.LabelTextSize;
			c.Rules = { Rule(UiState::Hovered, Fill(p.SurfaceHover)), Rule(UiState::Pressed, Fill(p.SurfacePressed)),
				Rule(UiState::Focused, FocusRing(p, m)), Rule(UiState::Disabled, Faded(m.DisabledOpacity)) };
			break;
		case UiThemeClass::DropdownArrow:
			s.Width = Px(m.DropdownArrowSize);
			s.Height = Px(m.DropdownArrowSize * 0.5f);
			s.Background = p.TextMuted;
			s.CornerRadius = m.DropdownArrowSize * 0.25f;
			break;
		case UiThemeClass::RadioOption:
			s.Gap = m.Spacing * 0.75f;
			c.TextSize = m.LabelTextSize;
			c.Rules = { Rule(UiState::Disabled, Faded(m.DisabledOpacity)) };
			break;
		case UiThemeClass::RadioCircle:
		{
			s.Width = Px(m.RadioSize);
			s.Height = Px(m.RadioSize);
			s.Background = p.Surface;
			s.BorderWidth = m.BorderWidth;
			s.BorderColor = p.Border;
			s.CornerRadius = m.RadioSize * 0.5f;
			UiVisual on;
			on.BorderColor = p.Accent;
			on.BorderWidth = std::max(m.BorderWidth, m.FocusWidth);
			c.Rules = { Rule(UiState::Hovered, Fill(p.SurfaceHover)), Rule(UiState::Pressed, Fill(p.SurfacePressed)),
				Rule(UiState::Checked, on), Rule(UiState::Focused, FocusRing(p, m)) };
			break;
		}
		case UiThemeClass::RadioDot:
			s.Width = Px(m.RadioSize * 0.5f);
			s.Height = Px(m.RadioSize * 0.5f);
			s.Background = p.Accent;
			s.CornerRadius = m.RadioSize * 0.25f;
			s.Opacity = 0.0f;
			c.Rules = { Rule(UiState::Checked, Faded(1.0f)) };
			break;
		case UiThemeClass::Tooltip:
			s.Background = p.Tooltip;
			s.BorderWidth = m.BorderWidth;
			s.BorderColor = WithAlpha(p.Border, 0.6f);
			s.CornerRadius = std::min(m.CornerRadius, 4.0f);
			s.Padding = m.TooltipPadding;
			c.TextSize = m.TooltipTextSize;
			break;
		case UiThemeClass::ModalScrim:
			s.Background = p.Scrim;
			break;
		case UiThemeClass::Dialog:
			s.Background = p.Popup;
			s.BorderWidth = m.BorderWidth;
			s.BorderColor = p.Border;
			s.CornerRadius = m.CornerRadius;
			s.Padding = { m.Spacing * 2.0f, m.Spacing * 2.0f, m.Spacing * 2.0f, m.Spacing * 2.0f };
			s.Gap = m.Spacing * 1.5f;
			s.MinSize = { m.DialogMinWidth, 0.0f };
			break;
		case UiThemeClass::DialogTitle:
			c.TextSize = m.DialogTitleTextSize;
			break;
		default:
			break;
		}
		if (Customize)
		{
			Customize(themeClass, c);
		}
		return c;
	}

	std::array<UiClassStyle, static_cast<std::size_t>(UiThemeClass::Count)> UiTheme::Build() const
	{
		std::array<UiClassStyle, static_cast<std::size_t>(UiThemeClass::Count)> classes;
		for (std::size_t i = 0; i < classes.size(); ++i)
		{
			classes[i] = Class(static_cast<UiThemeClass>(i));
			Internal::ValidateStyle(classes[i].Style);
			if (!std::isfinite(classes[i].TextSize) || classes[i].TextSize <= 0.0f || classes[i].TextSize > 16384.0f)
			{
				throw std::invalid_argument("Invalid UI theme text size");
			}
			for (const auto& rule : classes[i].Rules)
			{
				Internal::ValidateVisual(rule.Visual);
			}
		}
		return classes;
	}
} // namespace Swim::UI
