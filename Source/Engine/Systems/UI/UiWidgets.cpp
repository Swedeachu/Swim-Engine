#include "Engine/Systems/UI/UiWidgets.h"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace Swim::UI
{
	namespace
	{
		const UiTheme& ThemeOf(const UiDocument& document)
		{
			return *document.GetTheme();
		}

		std::shared_ptr<const Text::FontCollection> ThemeFonts(const UiDocument& document)
		{
			const auto& fonts = ThemeOf(document).Fonts;
			if (!fonts)
			{
				throw std::logic_error("Themed UI text needs UiTheme::Fonts");
			}
			return fonts;
		}

		float ThemeTextSize(const UiDocument& document, UiThemeClass themeClass)
		{
			return ThemeOf(document).Class(themeClass).TextSize;
		}

		UiNodeId CreateStyled(UiDocument& document, UiNodeId parent, const UiStyle& style)
		{
			const auto node = document.Create(parent);
			document.SetStyle(node, style);
			return node;
		}

		UiNodeId ThemedLabel(UiDocument& document, UiNodeId parent, std::string text)
		{
			const auto node = document.Create(parent);
			document.SetText(node, ThemeFonts(document), std::move(text), ThemeTextSize(document, UiThemeClass::Label));
			document.SetThemeClass(node, UiThemeClass::Label);
			return node;
		}
	} // namespace

	UiNodeId CreatePanel(UiDocument& document, UiNodeId parent, UiFlow flow)
	{
		UiStyle style;
		style.Flow = flow;
		const auto node = CreateStyled(document, parent, style);
		document.SetThemeClass(node, UiThemeClass::Panel);
		return node;
	}

	UiNodeId CreateLabel(UiDocument& document, UiNodeId parent, std::string text)
	{
		return ThemedLabel(document, parent, std::move(text));
	}

	UiNodeId CreateButton(UiDocument& document, UiNodeId parent, std::string label)
	{
		UiStyle style;
		style.TextAlign = Text::TextAlign::Center;
		const auto node = CreateStyled(document, parent, style);
		document.SetText(node, ThemeFonts(document), std::move(label), ThemeTextSize(document, UiThemeClass::Button));
		UiControl control;
		control.Kind = UiControlKind::Button;
		document.SetControl(node, control);
		document.SetThemeClass(node, UiThemeClass::Button);
		return node;
	}

	UiNodeId CreateTextField(UiDocument& document, UiNodeId parent, const UiTextEditOptions& options)
	{
		UiStyle style;
		style.Clip = true;
		style.TextWrap = options.Multiline ? Text::TextWrap::Word : Text::TextWrap::None;
		const auto node = CreateStyled(document, parent, style);
		document.SetText(node, ThemeFonts(document), {}, ThemeTextSize(document, UiThemeClass::TextField));
		document.SetEditable(node, true, options);
		document.SetThemeClass(node, UiThemeClass::TextField);
		return node;
	}

	UiNodeId CreateCheckbox(UiDocument& document, UiNodeId parent, std::string label, UiCheckState state)
	{
		UiStyle row;
		row.Flow = UiFlow::Row;
		row.AlignItems = UiAlign::Center;
		const auto root = CreateStyled(document, parent, row);
		UiStyle overlay;
		overlay.Flow = UiFlow::Overlay;
		overlay.AlignItems = UiAlign::Center;
		const auto box = CreateStyled(document, root, overlay);
		const auto mark = document.Create(box);
		const auto mixed = document.Create(box);
		UiControl control;
		control.Kind = UiControlKind::Checkbox;
		control.Check = state;
		control.Parts.Track = box;
		control.Parts.Mark = mark;
		control.Parts.Mixed = mixed;
		if (!label.empty())
		{
			control.Parts.Label = ThemedLabel(document, root, std::move(label));
		}
		document.SetControl(root, control);
		document.SetThemeClass(root, UiThemeClass::Checkbox);
		document.SetThemeClass(box, UiThemeClass::CheckBox);
		document.SetThemeClass(mark, UiThemeClass::CheckMark);
		document.SetThemeClass(mixed, UiThemeClass::CheckMixed);
		return root;
	}

	UiNodeId CreateToggle(UiDocument& document, UiNodeId parent, std::string label, bool on)
	{
		UiStyle row;
		row.Flow = UiFlow::Row;
		row.AlignItems = UiAlign::Center;
		const auto root = CreateStyled(document, parent, row);
		UiStyle overlay;
		overlay.Flow = UiFlow::Overlay;
		const auto track = CreateStyled(document, root, overlay);
		const auto knob = document.Create(track);
		UiControl control;
		control.Kind = UiControlKind::Toggle;
		control.Check = on ? UiCheckState::Checked : UiCheckState::Unchecked;
		control.Parts.Track = track;
		control.Parts.Thumb = knob;
		if (!label.empty())
		{
			control.Parts.Label = ThemedLabel(document, root, std::move(label));
		}
		document.SetControl(root, control);
		document.SetThemeClass(root, UiThemeClass::Toggle);
		document.SetThemeClass(track, UiThemeClass::ToggleTrack);
		document.SetThemeClass(knob, UiThemeClass::ToggleKnob);
		return root;
	}

	UiNodeId CreateSlider(UiDocument& document, UiNodeId parent, const UiSliderDesc& desc)
	{
		if (desc.ShowValue)
		{
			UiStyle row;
			row.Flow = UiFlow::Row;
			row.AlignItems = UiAlign::Center;
			row.Gap = ThemeOf(document).Metrics.Spacing;
			parent = CreateStyled(document, parent, row);
		}
		UiStyle overlay;
		overlay.Flow = UiFlow::Overlay;
		const auto root = CreateStyled(document, parent, overlay);
		const auto track = document.Create(root);
		std::vector<std::pair<UiNodeId, float>> ticks;
		for (std::uint32_t i = 0; desc.Ticks >= 2 && i < desc.Ticks; ++i)
		{
			ticks.emplace_back(document.Create(root), desc.Min + (desc.Max - desc.Min) * float(i) / float(desc.Ticks - 1));
		}
		const auto fill = document.Create(root);
		const auto thumb = document.Create(root);
		UiControl control;
		control.Kind = UiControlKind::Slider;
		control.Orientation = desc.Orientation;
		control.Min = desc.Min;
		control.Max = desc.Max;
		control.Value = desc.Value;
		control.Step = desc.Step;
		control.PageStep = desc.PageStep;
		control.TrackClick = desc.TrackClick;
		control.Parts.Track = track;
		control.Parts.Fill = fill;
		control.Parts.Thumb = thumb;
		if (desc.ShowValue)
		{
			const auto label = document.Create(parent);
			UiStyle end;
			end.TextAlign = Text::TextAlign::End;
			document.SetStyle(label, end);
			document.SetText(label, ThemeFonts(document), {}, ThemeTextSize(document, UiThemeClass::SliderValue));
			document.SetThemeClass(label, UiThemeClass::SliderValue);
			control.Parts.Label = label;
			control.LabelDecimals = std::clamp(desc.Decimals, 0, 9);
		}
		document.SetControl(root, control);
		document.SetThemeClass(root, UiThemeClass::Slider);
		document.SetThemeClass(track, UiThemeClass::SliderTrack);
		for (const auto& [tick, value] : ticks)
		{
			document.SetPartRole(tick, root, UiPartRole::Tick, value);
			document.SetThemeClass(tick, UiThemeClass::SliderTick);
		}
		document.SetThemeClass(fill, UiThemeClass::SliderFill);
		document.SetThemeClass(thumb, UiThemeClass::SliderThumb);
		return root;
	}

	UiNodeId CreateScrollBar(UiDocument& document, UiNodeId parent, UiNodeId target, const UiScrollBarDesc& desc)
	{
		UiStyle bar;
		bar.Flow = UiFlow::Overlay;
		const auto root = CreateStyled(document, parent, bar);
		const auto thumb = document.Create(root);
		const auto decrement = desc.StepButtons ? document.Create(root) : UiNodeId{};
		const auto increment = desc.StepButtons ? document.Create(root) : UiNodeId{};
		UiControl control;
		control.Kind = UiControlKind::ScrollBar;
		control.Orientation = desc.Orientation;
		control.Visibility = desc.Visibility;
		control.TrackClick = desc.TrackClick;
		control.ScrollTarget = target;
		control.MinThumbLength = ThemeOf(document).Metrics.ScrollBarMinThumb;
		control.Parts.Thumb = thumb;
		control.Parts.Decrement = decrement;
		control.Parts.Increment = increment;
		document.SetControl(root, control);
		document.SetThemeClass(root, UiThemeClass::ScrollBar);
		document.SetThemeClass(thumb, UiThemeClass::ScrollThumb);
		for (const auto button : { decrement, increment })
		{
			if (button)
			{
				document.SetThemeClass(button, UiThemeClass::ScrollButton);
			}
		}
		return root;
	}

	UiScrollArea CreateScrollArea(UiDocument& document, UiNodeId parent, const UiStyle& rootStyle, bool vertical, bool horizontal,
		UiScrollBarVisibility visibility, bool stepButtons)
	{
		UiScrollArea area;
		const bool overlay = visibility == UiScrollBarVisibility::Overlay;
		const float thickness = ThemeOf(document).Metrics.ScrollBarThickness;
		auto root = rootStyle;
		root.Flow = overlay ? UiFlow::Overlay : UiFlow::Column;
		root.AlignItems = UiAlign::Stretch;
		area.Root = CreateStyled(document, parent, root);
		UiNodeId row = area.Root;
		if (!overlay)
		{
			UiStyle rowStyle;
			rowStyle.Flow = UiFlow::Row;
			rowStyle.AlignItems = UiAlign::Stretch;
			rowStyle.Grow = 1.0f;
			rowStyle.Shrink = 1.0f;
			row = CreateStyled(document, area.Root, rowStyle);
		}
		UiStyle viewport;
		viewport.Clip = true;
		viewport.Grow = 1.0f;
		viewport.Shrink = 1.0f;
		viewport.AlignSelf = UiAlign::Stretch;
		area.Viewport = CreateStyled(document, row, viewport);
		UiScrollBarDesc barDesc;
		barDesc.Visibility = visibility;
		barDesc.StepButtons = stepButtons;
		const auto place = [&](UiNodeId bar, bool isVertical)
		{
			if (!overlay)
			{
				return;
			}
			auto style = document.GetStyle(bar);
			style.Absolute = true;
			style.AnchorMin = isVertical ? UiPoint{ 1.0f, 0.0f } : UiPoint{ 0.0f, 1.0f };
			style.AnchorMax = isVertical ? UiPoint{ 1.0f, 1.0f } : UiPoint{ 1.0f, 1.0f };
			style.Pivot = isVertical ? UiPoint{ 1.0f, 0.0f } : UiPoint{ 0.0f, 1.0f };
			if (vertical && horizontal)
			{
				(isVertical ? style.Margin.Bottom : style.Margin.Right) = thickness;
			}
			document.SetStyle(bar, style);
		};
		if (vertical)
		{
			barDesc.Orientation = UiOrientation::Vertical;
			area.Vertical = CreateScrollBar(document, row, area.Viewport, barDesc);
			place(area.Vertical, true);
		}
		if (horizontal)
		{
			barDesc.Orientation = UiOrientation::Horizontal;
			area.Horizontal = CreateScrollBar(document, area.Root, area.Viewport, barDesc);
			place(area.Horizontal, false);
			if (!overlay && vertical)
			{
				auto style = document.GetStyle(area.Horizontal);
				style.Margin.Right = thickness; // Leaves the corner under the vertical bar.
				document.SetStyle(area.Horizontal, style);
			}
		}
		return area;
	}

	UiNodeId CreateLabel(UiDocument& document, UiNodeId parent, std::shared_ptr<const Text::FontCollection> fonts, std::string text,
		float size, const UiStyle& style)
	{
		const auto node = CreateStyled(document, parent, style);
		document.SetText(node, std::move(fonts), std::move(text), size);
		return node;
	}

	UiNodeId CreateImage(UiDocument& document, UiNodeId parent, const UiImage& image, const UiStyle& style)
	{
		const auto node = CreateStyled(document, parent, style);
		document.SetImage(node, image);
		return node;
	}

	UiNodeId CreateScrollView(UiDocument& document, UiNodeId parent, const UiStyle& style)
	{
		auto clipped = style;
		clipped.Clip = true;
		return CreateStyled(document, parent, clipped);
	}

	UiNodeId CreateTextField(UiDocument& document, UiNodeId parent, std::shared_ptr<const Text::FontCollection> fonts, float size,
		const UiTextEditOptions& options, const UiStyle& style)
	{
		auto field = style;
		field.Clip = true;
		if (!options.Multiline)
		{
			field.TextWrap = Text::TextWrap::None;
		}
		const auto node = CreateStyled(document, parent, field);
		document.SetText(node, std::move(fonts), {}, size);
		document.SetEditable(node, true, options);
		return node;
	}
} // namespace Swim::UI
