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

	UiTheme::UiTheme()
	{
		builders.emplace(UiThemeClass::None,
			[](const UiTheme&, UiClassStyle&)
			{
			});
		builders.emplace(UiThemeClass::Panel,
			[](const UiTheme& theme, UiClassStyle& c)
			{
				[[maybe_unused]] const auto& p = theme.Palette;
				[[maybe_unused]] const auto& m = theme.Metrics;
				[[maybe_unused]] auto& s = c.Style;
				s.Background = p.Panel;
				s.CornerRadius = m.CornerRadius;
				s.Padding = { m.Spacing, m.Spacing, m.Spacing, m.Spacing };
				s.Gap = m.Spacing;
			});

		builders.emplace(UiThemeClass::Label,
			[](const UiTheme& theme, UiClassStyle& c)
			{
				[[maybe_unused]] const auto& p = theme.Palette;
				[[maybe_unused]] const auto& m = theme.Metrics;
				[[maybe_unused]] auto& s = c.Style;
				c.TextSize = m.LabelTextSize;
				UiVisual muted;
				muted.TextColor = p.TextMuted;
				c.Rules.push_back(Rule(UiState::Disabled, muted));
			});

		builders.emplace(UiThemeClass::Button,
			[](const UiTheme& theme, UiClassStyle& c)
			{
				[[maybe_unused]] const auto& p = theme.Palette;
				[[maybe_unused]] const auto& m = theme.Metrics;
				[[maybe_unused]] auto& s = c.Style;
				s.Padding = m.ButtonPadding;
				s.MinSize = { 0.0f, m.ControlHeight };
				s.Background = p.Surface;
				s.CornerRadius = m.CornerRadius;
				s.BorderColor = p.Focus;
				c.Rules = { Rule(UiState::Hovered, Fill(p.SurfaceHover)), Rule(UiState::Pressed, Fill(p.SurfacePressed)),
					Rule(UiState::Focused, FocusRing(p, m)), Rule(UiState::Disabled, Faded(m.DisabledOpacity)) };
			});

		builders.emplace(UiThemeClass::TextField,
			[](const UiTheme& theme, UiClassStyle& c)
			{
				[[maybe_unused]] const auto& p = theme.Palette;
				[[maybe_unused]] const auto& m = theme.Metrics;
				[[maybe_unused]] auto& s = c.Style;
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
			});

		builders.emplace(UiThemeClass::ScrollView,
			[](const UiTheme& theme, UiClassStyle& c)
			{
				[[maybe_unused]] const auto& p = theme.Palette;
				[[maybe_unused]] const auto& m = theme.Metrics;
				[[maybe_unused]] auto& s = c.Style;
			});

		builders.emplace(UiThemeClass::ScrollBar,
			[](const UiTheme& theme, UiClassStyle& c)
			{
				[[maybe_unused]] const auto& p = theme.Palette;
				[[maybe_unused]] const auto& m = theme.Metrics;
				[[maybe_unused]] auto& s = c.Style;
				s.Height = Px(m.ScrollBarThickness);
				s.Background = WithAlpha(p.Track, 0.35f);
				s.CornerRadius = m.ScrollBarThickness * 0.5f;
				c.Rules = { Rule(UiState::Hovered, Fill(WithAlpha(p.Track, 0.6f))) };
			});

		builders.emplace(UiThemeClass::ScrollThumb,
			[](const UiTheme& theme, UiClassStyle& c)
			{
				[[maybe_unused]] const auto& p = theme.Palette;
				[[maybe_unused]] const auto& m = theme.Metrics;
				[[maybe_unused]] auto& s = c.Style;
				s.Background = WithAlpha(p.TextMuted, 0.7f);
				s.CornerRadius = m.ScrollBarThickness * 0.5f;
				c.Rules = { Rule(UiState::Hovered, Fill(WithAlpha(p.Text, 0.8f))), Rule(UiState::Dragging, Fill(p.Accent)) };
			});

		builders.emplace(UiThemeClass::Checkbox,
			[](const UiTheme& theme, UiClassStyle& c)
			{
				[[maybe_unused]] const auto& p = theme.Palette;
				[[maybe_unused]] const auto& m = theme.Metrics;
				[[maybe_unused]] auto& s = c.Style;
				s.Gap = m.Spacing * 0.75f;
				c.TextSize = m.LabelTextSize;
				c.Rules = { Rule(UiState::Disabled, Faded(m.DisabledOpacity)) };
			});

		builders.emplace(UiThemeClass::Toggle,
			[](const UiTheme& theme, UiClassStyle& c)
			{
				[[maybe_unused]] const auto& p = theme.Palette;
				[[maybe_unused]] const auto& m = theme.Metrics;
				[[maybe_unused]] auto& s = c.Style;
				s.Gap = m.Spacing * 0.75f;
				c.TextSize = m.LabelTextSize;
				c.Rules = { Rule(UiState::Disabled, Faded(m.DisabledOpacity)) };
			});

		builders.emplace(UiThemeClass::CheckBox,
			[](const UiTheme& theme, UiClassStyle& c)
			{
				[[maybe_unused]] const auto& p = theme.Palette;
				[[maybe_unused]] const auto& m = theme.Metrics;
				[[maybe_unused]] auto& s = c.Style;
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
			});

		builders.emplace(UiThemeClass::CheckMark,
			[](const UiTheme& theme, UiClassStyle& c)
			{
				[[maybe_unused]] const auto& p = theme.Palette;
				[[maybe_unused]] const auto& m = theme.Metrics;
				[[maybe_unused]] auto& s = c.Style;
				s.Width = Px(m.CheckboxSize * 0.5f);
				s.Height = Px(m.CheckboxSize * 0.5f);
				s.Background = p.OnAccent;
				s.CornerRadius = 2.0f;
				s.Opacity = 0.0f;
				c.Rules = { Rule(UiState::Checked, Faded(1.0f)) };
			});

		builders.emplace(UiThemeClass::CheckMixed,
			[](const UiTheme& theme, UiClassStyle& c)
			{
				[[maybe_unused]] const auto& p = theme.Palette;
				[[maybe_unused]] const auto& m = theme.Metrics;
				[[maybe_unused]] auto& s = c.Style;
				s.Width = Px(m.CheckboxSize * 0.55f);
				s.Height = Px(std::max(2.0f, m.CheckboxSize / 9.0f));
				s.Background = p.OnAccent;
				s.CornerRadius = 1.0f;
				s.Opacity = 0.0f;
				c.Rules = { Rule(UiState::Mixed, Faded(1.0f)) };
			});

		builders.emplace(UiThemeClass::ToggleTrack,
			[](const UiTheme& theme, UiClassStyle& c)
			{
				[[maybe_unused]] const auto& p = theme.Palette;
				[[maybe_unused]] const auto& m = theme.Metrics;
				[[maybe_unused]] auto& s = c.Style;
				s.Width = Px(m.ToggleWidth);
				s.Height = Px(m.ToggleHeight);
				s.Padding = { m.ToggleInset, m.ToggleInset, m.ToggleInset, m.ToggleInset };
				s.Background = p.Track;
				s.CornerRadius = m.ToggleHeight * 0.5f;
				c.Rules = { Rule(UiState::Hovered, Fill(p.SurfaceHover)), Rule(UiState::Checked, Fill(p.Accent)),
					Rule(UiState::Checked | UiState::Hovered, Fill(p.AccentHover)), Rule(UiState::Focused, FocusRing(p, m)) };
			});

		builders.emplace(UiThemeClass::ToggleKnob,
			[](const UiTheme& theme, UiClassStyle& c)
			{
				[[maybe_unused]] const auto& p = theme.Palette;
				[[maybe_unused]] const auto& m = theme.Metrics;
				[[maybe_unused]] auto& s = c.Style;
				const float knob = std::max(0.0f, m.ToggleHeight - 2.0f * m.ToggleInset);
				s.Width = Px(knob);
				s.Height = Px(knob);
				s.Background = p.Text;
				s.CornerRadius = knob * 0.5f;
				c.Rules = { Rule(UiState::Checked, Fill(p.OnAccent)) };
			});

		builders.emplace(UiThemeClass::Slider,
			[](const UiTheme& theme, UiClassStyle& c)
			{
				[[maybe_unused]] const auto& p = theme.Palette;
				[[maybe_unused]] const auto& m = theme.Metrics;
				[[maybe_unused]] auto& s = c.Style;
				s.Width = Px(m.SliderLength);
				s.Height = Px(std::max(m.SliderThumb, m.SliderTrack));
				c.Rules = { Rule(UiState::Disabled, Faded(m.DisabledOpacity)) };
			});

		builders.emplace(UiThemeClass::SliderTrack,
			[](const UiTheme& theme, UiClassStyle& c)
			{
				[[maybe_unused]] const auto& p = theme.Palette;
				[[maybe_unused]] const auto& m = theme.Metrics;
				[[maybe_unused]] auto& s = c.Style;
				s.Height = Px(m.SliderTrack);
				s.Background = p.Track;
				s.CornerRadius = m.SliderTrack * 0.5f;
			});

		builders.emplace(UiThemeClass::SliderFill,
			[](const UiTheme& theme, UiClassStyle& c)
			{
				[[maybe_unused]] const auto& p = theme.Palette;
				[[maybe_unused]] const auto& m = theme.Metrics;
				[[maybe_unused]] auto& s = c.Style;
				s.Height = Px(m.SliderTrack);
				s.Background = p.Accent;
				s.CornerRadius = m.SliderTrack * 0.5f;
				c.Rules = { Rule(UiState::Hovered, Fill(p.AccentHover)) };
			});

		builders.emplace(UiThemeClass::SliderThumb,
			[](const UiTheme& theme, UiClassStyle& c)
			{
				[[maybe_unused]] const auto& p = theme.Palette;
				[[maybe_unused]] const auto& m = theme.Metrics;
				[[maybe_unused]] auto& s = c.Style;
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
			});

		builders.emplace(UiThemeClass::SliderTick,
			[](const UiTheme& theme, UiClassStyle& c)
			{
				[[maybe_unused]] const auto& p = theme.Palette;
				[[maybe_unused]] const auto& m = theme.Metrics;
				[[maybe_unused]] auto& s = c.Style;
				s.Width = Px(std::max(1.0f, m.SliderTrack * 0.5f));
				s.Height = Px(m.SliderThumb * 0.75f);
				s.Background = WithAlpha(p.TextMuted, 0.6f);
				s.CornerRadius = 1.0f;
			});

		builders.emplace(UiThemeClass::SliderValue,
			[](const UiTheme& theme, UiClassStyle& c)
			{
				[[maybe_unused]] const auto& p = theme.Palette;
				[[maybe_unused]] const auto& m = theme.Metrics;
				[[maybe_unused]] auto& s = c.Style;
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
			});

		builders.emplace(UiThemeClass::ScrollButton,
			[](const UiTheme& theme, UiClassStyle& c)
			{
				[[maybe_unused]] const auto& p = theme.Palette;
				[[maybe_unused]] const auto& m = theme.Metrics;
				[[maybe_unused]] auto& s = c.Style;
				s.Background = WithAlpha(p.Track, 0.5f);
				s.CornerRadius = m.ScrollBarThickness * 0.25f;
				c.Rules = { Rule(UiState::Hovered, Fill(WithAlpha(p.Track, 0.85f))), Rule(UiState::Pressed, Fill(p.Accent)) };
			});

		builders.emplace(UiThemeClass::Popup,
			[](const UiTheme& theme, UiClassStyle& c)
			{
				[[maybe_unused]] const auto& p = theme.Palette;
				[[maybe_unused]] const auto& m = theme.Metrics;
				[[maybe_unused]] auto& s = c.Style;
				s.Background = p.Popup;
				s.BorderWidth = m.BorderWidth;
				s.BorderColor = p.Border;
				s.CornerRadius = m.CornerRadius;
				s.Padding = { m.PopupPadding, m.PopupPadding, m.PopupPadding, m.PopupPadding };
				s.MaxSize = { Internal::MaxLogical, std::max(m.PopupMaxHeight, 1.0f) };
			});

		builders.emplace(UiThemeClass::MenuItem,
			[](const UiTheme& theme, UiClassStyle& c)
			{
				[[maybe_unused]] const auto& p = theme.Palette;
				[[maybe_unused]] const auto& m = theme.Metrics;
				[[maybe_unused]] auto& s = c.Style;
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
			});

		builders.emplace(UiThemeClass::MenuSeparator,
			[](const UiTheme& theme, UiClassStyle& c)
			{
				[[maybe_unused]] const auto& p = theme.Palette;
				[[maybe_unused]] const auto& m = theme.Metrics;
				[[maybe_unused]] auto& s = c.Style;
				s.Height = Px(std::max(1.0f, m.BorderWidth));
				s.Background = WithAlpha(p.Border, 0.8f);
			});

		builders.emplace(UiThemeClass::ListView,
			[](const UiTheme& theme, UiClassStyle& c)
			{
				[[maybe_unused]] const auto& p = theme.Palette;
				[[maybe_unused]] const auto& m = theme.Metrics;
				[[maybe_unused]] auto& s = c.Style;
				s.Background = p.Field;
				s.BorderWidth = m.BorderWidth;
				s.BorderColor = p.Border;
				s.CornerRadius = std::min(m.CornerRadius, 4.0f);
				s.Padding = { m.BorderWidth, m.BorderWidth, m.BorderWidth, m.BorderWidth };
				c.Rules = { Rule(UiState::Focused, FocusRing(p, m)), Rule(UiState::Disabled, Faded(m.DisabledOpacity)) };
			});

		builders.emplace(UiThemeClass::Dropdown,
			[](const UiTheme& theme, UiClassStyle& c)
			{
				[[maybe_unused]] const auto& p = theme.Palette;
				[[maybe_unused]] const auto& m = theme.Metrics;
				[[maybe_unused]] auto& s = c.Style;
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
			});

		builders.emplace(UiThemeClass::DropdownArrow,
			[](const UiTheme& theme, UiClassStyle& c)
			{
				[[maybe_unused]] const auto& p = theme.Palette;
				[[maybe_unused]] const auto& m = theme.Metrics;
				[[maybe_unused]] auto& s = c.Style;
				s.Width = Px(m.DropdownArrowSize);
				s.Height = Px(m.DropdownArrowSize * 0.5f);
				s.Background = p.TextMuted;
				s.CornerRadius = m.DropdownArrowSize * 0.25f;
			});

		builders.emplace(UiThemeClass::RadioOption,
			[](const UiTheme& theme, UiClassStyle& c)
			{
				[[maybe_unused]] const auto& p = theme.Palette;
				[[maybe_unused]] const auto& m = theme.Metrics;
				[[maybe_unused]] auto& s = c.Style;
				s.Gap = m.Spacing * 0.75f;
				c.TextSize = m.LabelTextSize;
				c.Rules = { Rule(UiState::Disabled, Faded(m.DisabledOpacity)) };
			});

		builders.emplace(UiThemeClass::RadioCircle,
			[](const UiTheme& theme, UiClassStyle& c)
			{
				[[maybe_unused]] const auto& p = theme.Palette;
				[[maybe_unused]] const auto& m = theme.Metrics;
				[[maybe_unused]] auto& s = c.Style;
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
			});

		builders.emplace(UiThemeClass::RadioDot,
			[](const UiTheme& theme, UiClassStyle& c)
			{
				[[maybe_unused]] const auto& p = theme.Palette;
				[[maybe_unused]] const auto& m = theme.Metrics;
				[[maybe_unused]] auto& s = c.Style;
				s.Width = Px(m.RadioSize * 0.5f);
				s.Height = Px(m.RadioSize * 0.5f);
				s.Background = p.Accent;
				s.CornerRadius = m.RadioSize * 0.25f;
				s.Opacity = 0.0f;
				c.Rules = { Rule(UiState::Checked, Faded(1.0f)) };
			});

		builders.emplace(UiThemeClass::Tooltip,
			[](const UiTheme& theme, UiClassStyle& c)
			{
				[[maybe_unused]] const auto& p = theme.Palette;
				[[maybe_unused]] const auto& m = theme.Metrics;
				[[maybe_unused]] auto& s = c.Style;
				s.Background = p.Tooltip;
				s.BorderWidth = m.BorderWidth;
				s.BorderColor = WithAlpha(p.Border, 0.6f);
				s.CornerRadius = std::min(m.CornerRadius, 4.0f);
				s.Padding = m.TooltipPadding;
				c.TextSize = m.TooltipTextSize;
			});

		builders.emplace(UiThemeClass::ModalScrim,
			[](const UiTheme& theme, UiClassStyle& c)
			{
				[[maybe_unused]] const auto& p = theme.Palette;
				[[maybe_unused]] const auto& m = theme.Metrics;
				[[maybe_unused]] auto& s = c.Style;
				s.Background = p.Scrim;
			});

		builders.emplace(UiThemeClass::Dialog,
			[](const UiTheme& theme, UiClassStyle& c)
			{
				[[maybe_unused]] const auto& p = theme.Palette;
				[[maybe_unused]] const auto& m = theme.Metrics;
				[[maybe_unused]] auto& s = c.Style;
				s.Background = p.Popup;
				s.BorderWidth = m.BorderWidth;
				s.BorderColor = p.Border;
				s.CornerRadius = m.CornerRadius;
				s.Padding = { m.Spacing * 2.0f, m.Spacing * 2.0f, m.Spacing * 2.0f, m.Spacing * 2.0f };
				s.Gap = m.Spacing * 1.5f;
				s.MinSize = { m.DialogMinWidth, 0.0f };
			});

		builders.emplace(UiThemeClass::DialogTitle,
			[](const UiTheme& theme, UiClassStyle& c)
			{
				[[maybe_unused]] const auto& p = theme.Palette;
				[[maybe_unused]] const auto& m = theme.Metrics;
				[[maybe_unused]] auto& s = c.Style;
				c.TextSize = m.DialogTitleTextSize;
			});
	}

	UiThemeClass UiTheme::RegisterClass(std::string name, ClassBuilder builder)
	{
		if (name.empty() || !builder || names.contains(name))
		{
			throw std::invalid_argument("UI theme class needs a unique name and builder");
		}

		// Fixed FNV-1a, separate from the built-in ID range. Never use std::hash for IDs.
		std::uint32_t hash = 2166136261u;

		for (const unsigned char c : name)
		{
			hash = (hash ^ c) * 16777619u;
		}

		const auto id = static_cast<UiThemeClass>(hash | 0x80000000u);

		if (builders.contains(id))
		{
			throw std::invalid_argument("UI theme class ID collision; choose another name");
		}

		builders.emplace(id, std::move(builder));
		names.emplace(std::move(name), id);
		return id;
	}

	void UiTheme::ReplaceClass(UiThemeClass themeClass, ClassBuilder builder)
	{
		if (!builder || !builders.contains(themeClass))
		{
			throw std::invalid_argument("Unknown UI theme class or empty builder");
		}

		builders.at(themeClass) = std::move(builder);
	}

	UiThemeClass UiTheme::FindClass(std::string_view name) const
	{
		return names.at(std::string(name));
	}

	UiClassStyle UiTheme::Class(UiThemeClass themeClass) const
	{
		const auto& p = Palette;
		const auto& m = Metrics;
		const auto builder = builders.find(themeClass);

		if (builder == builders.end())
		{
			throw std::invalid_argument("Unknown UI theme class");
		}

		UiClassStyle c;
		auto& s = c.Style;
		s.TransitionSeconds = m.TransitionSeconds;
		s.TextColor = p.Text;
		s.SelectionColor = p.Selection;
		s.CaretColor = p.Caret;
		c.TextSize = m.TextSize;

		builder->second(*this, c);

		if (Customize)
		{
			Customize(themeClass, c);
		}

		return c;
	}

	std::unordered_map<UiThemeClass, UiClassStyle> UiTheme::Build() const
	{
		std::unordered_map<UiThemeClass, UiClassStyle> classes;

		for (const auto& [id, builder] : builders)
		{
			(void)builder;
			auto& result = classes[id];
			result = Class(id);
			Internal::ValidateStyle(result.Style);

			if (!std::isfinite(result.TextSize) || result.TextSize <= 0.0f || result.TextSize > 16384.0f)
			{
				throw std::invalid_argument("Invalid UI theme text size");
			}

			for (const auto& rule : result.Rules)
			{
				Internal::ValidateVisual(rule.Visual);
			}
		}

		return classes;
	}

} // namespace Swim::UI
