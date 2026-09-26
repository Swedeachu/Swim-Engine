#include "Engine/Systems/UI/UiWidgets.h"

#include <algorithm>
#include <cmath>
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
			if (desc.EditableValue)
			{
				auto field = document.GetStyle(label);
				field.Clip = true;
				field.TextWrap = Text::TextWrap::None;
				document.SetStyle(label, field);
				UiTextEditOptions options;
				options.MaxBytes = 64;
				document.SetEditable(label, true, options);
			}
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

namespace Swim::UI
{
	namespace
	{
		UiNodeId Styled(UiDocument& document, UiNodeId parent, const UiStyle& style)
		{
			const auto node = document.Create(parent);
			document.SetStyle(node, style);
			return node;
		}

		std::shared_ptr<const Text::FontCollection> Fonts(const UiDocument& document)
		{
			const auto& fonts = document.GetTheme()->Fonts;
			if (!fonts)
			{
				throw std::logic_error("Themed UI text needs UiTheme::Fonts");
			}
			return fonts;
		}

		float TextSize(const UiDocument& document, UiThemeClass themeClass)
		{
			return document.GetTheme()->Class(themeClass).TextSize;
		}

		// A single-line text option row of an owner.
		UiNodeId TextOption(UiDocument& document, UiNodeId parent, UiNodeId owner, std::string text)
		{
			UiStyle style;
			style.TextWrap = Text::TextWrap::None;
			const auto row = Styled(document, parent, style);
			document.SetText(row, Fonts(document), std::move(text), TextSize(document, UiThemeClass::MenuItem));
			document.SetPartRole(row, owner, UiPartRole::Option, float(document.GetOptionCount(owner)));
			document.SetThemeClass(row, UiThemeClass::MenuItem);
			return row;
		}

		// A popup: hidden, placed by the document, blocking presses on what lies below.
		UiPopupList PopupList(UiDocument& document)
		{
			UiStyle rootStyle;
			rootStyle.Visible = false;
			rootStyle.Absolute = true;
			rootStyle.HitTest = true;
			const auto area = CreateScrollArea(document, document.GetRoot(), rootStyle, true, false, UiScrollBarVisibility::Overlay, false);
			auto items = document.GetStyle(area.Viewport);
			items.AlignItems = UiAlign::Stretch;
			document.SetStyle(area.Viewport, items);
			document.SetThemeClass(area.Root, UiThemeClass::Popup);
			return { area.Root, area.Viewport, area.Vertical };
		}
	} // namespace

	UiNodeId CreateRadioGroup(
		UiDocument& document, UiNodeId parent, const std::vector<std::string>& options, std::int32_t selected, UiOrientation orientation)
	{
		UiStyle style;
		style.Flow = orientation == UiOrientation::Horizontal ? UiFlow::Row : UiFlow::Column;
		style.Gap = document.GetTheme()->Metrics.Spacing * (orientation == UiOrientation::Horizontal ? 1.5f : 0.5f);
		const auto group = Styled(document, parent, style);
		UiControl control;
		control.Kind = UiControlKind::RadioGroup;
		control.Orientation = orientation;
		control.Value = -1.0f;
		document.SetControl(group, control);
		for (const auto& option : options)
		{
			AddRadioOption(document, group, option);
		}
		document.SetValue(group, float(selected));
		return group;
	}

	UiNodeId AddRadioOption(UiDocument& document, UiNodeId group, std::string label)
	{
		UiStyle rowStyle;
		rowStyle.Flow = UiFlow::Row;
		rowStyle.AlignItems = UiAlign::Center;
		const auto row = Styled(document, group, rowStyle);
		UiStyle overlay;
		overlay.Flow = UiFlow::Overlay;
		overlay.AlignItems = UiAlign::Center;
		const auto circle = Styled(document, row, overlay);
		const auto dot = document.Create(circle);
		if (!label.empty())
		{
			const auto text = document.Create(row);
			document.SetText(text, Fonts(document), std::move(label), TextSize(document, UiThemeClass::Label));
			document.SetThemeClass(text, UiThemeClass::Label);
		}
		document.SetPartRole(row, group, UiPartRole::Option, float(document.GetOptionCount(group)));
		document.SetThemeClass(row, UiThemeClass::RadioOption);
		document.SetThemeClass(circle, UiThemeClass::RadioCircle);
		document.SetThemeClass(dot, UiThemeClass::RadioDot);
		return row;
	}

	UiListView CreateListView(
		UiDocument& document, UiNodeId parent, const UiStyle& rootStyle, const std::vector<std::string>& items, std::int32_t selected)
	{
		const auto area = CreateScrollArea(document, parent, rootStyle, true, false, UiScrollBarVisibility::Auto, false);
		auto viewport = document.GetStyle(area.Viewport);
		viewport.AlignItems = UiAlign::Stretch;
		document.SetStyle(area.Viewport, viewport);
		UiControl control;
		control.Kind = UiControlKind::ListView;
		control.Orientation = UiOrientation::Vertical;
		control.Value = -1.0f;
		control.ScrollTarget = area.Viewport;
		document.SetControl(area.Root, control);
		document.SetThemeClass(area.Root, UiThemeClass::ListView, UiThemeApply::Paint); // The caller's size stays.
		UiListView list{ area.Root, area.Viewport, area.Vertical };
		for (const auto& item : items)
		{
			AddListItem(document, list, item);
		}
		document.SetValue(list.Root, float(selected));
		return list;
	}

	UiNodeId AddListItem(UiDocument& document, const UiListView& list, std::string text)
	{
		return TextOption(document, list.Viewport, list.Root, std::move(text));
	}

	UiDropdown CreateDropdown(
		UiDocument& document, UiNodeId parent, const std::vector<std::string>& options, std::int32_t selected, std::string placeholder)
	{
		UiDropdown dropdown;
		UiStyle row;
		row.Flow = UiFlow::Row;
		row.AlignItems = UiAlign::Center;
		dropdown.Root = Styled(document, parent, row);
		UiStyle labelStyle;
		labelStyle.Grow = 1.0f;
		labelStyle.Shrink = 1.0f;
		labelStyle.Clip = true;
		labelStyle.TextWrap = Text::TextWrap::None;
		dropdown.Label = Styled(document, dropdown.Root, labelStyle);
		document.SetText(dropdown.Label, Fonts(document), std::move(placeholder), TextSize(document, UiThemeClass::Dropdown));
		dropdown.Arrow = document.Create(dropdown.Root);
		dropdown.List = PopupList(document);
		UiControl control;
		control.Kind = UiControlKind::Dropdown;
		control.Value = -1.0f;
		control.ScrollTarget = dropdown.List.Items;
		control.Parts.Label = dropdown.Label;
		control.Parts.Popup = dropdown.List.Root;
		document.SetControl(dropdown.Root, control);
		document.SetThemeClass(dropdown.Root, UiThemeClass::Dropdown);
		document.SetThemeClass(dropdown.Label, UiThemeClass::Label, UiThemeApply::Text);
		document.SetThemeClass(dropdown.Arrow, UiThemeClass::DropdownArrow);
		for (const auto& option : options)
		{
			AddDropdownOption(document, dropdown, option);
		}
		document.SetValue(dropdown.Root, float(selected));
		return dropdown;
	}

	UiNodeId AddDropdownOption(UiDocument& document, const UiDropdown& dropdown, std::string text)
	{
		return TextOption(document, dropdown.List.Items, dropdown.Root, std::move(text));
	}

	UiPopupList CreateMenu(UiDocument& document)
	{
		return PopupList(document);
	}

	UiNodeId AddMenuItem(UiDocument& document, const UiPopupList& menu, std::string label)
	{
		UiStyle style;
		style.TextWrap = Text::TextWrap::None;
		const auto item = Styled(document, menu.Items, style);
		document.SetText(item, Fonts(document), std::move(label), TextSize(document, UiThemeClass::MenuItem));
		UiControl control;
		control.Kind = UiControlKind::Button;
		document.SetControl(item, control);
		document.SetThemeClass(item, UiThemeClass::MenuItem);
		return item;
	}

	UiNodeId AddMenuSeparator(UiDocument& document, const UiPopupList& menu)
	{
		UiStyle style;
		const float gap = document.GetTheme()->Metrics.PopupPadding;
		style.Margin = { 0.0f, gap, 0.0f, gap };
		const auto separator = Styled(document, menu.Items, style);
		document.SetThemeClass(separator, UiThemeClass::MenuSeparator);
		return separator;
	}

	void OpenMenu(UiDocument& document, const UiPopupList& menu, UiNodeId anchor, UiPopupSide side)
	{
		UiPopupDesc desc;
		desc.Anchor = anchor;
		desc.Side = side;
		desc.CloseOnActivate = true;
		document.OpenPopup(menu.Root, desc);
	}

	UiNodeId CreateTooltip(UiDocument& document, UiNodeId target, std::string text, float delaySeconds)
	{
		UiStyle style;
		style.Visible = false;
		style.Absolute = true;
		style.TextWrap = Text::TextWrap::None;
		const auto tooltip = Styled(document, document.GetRoot(), style);
		document.SetText(tooltip, Fonts(document), std::move(text), TextSize(document, UiThemeClass::Tooltip));
		document.SetThemeClass(tooltip, UiThemeClass::Tooltip);
		document.SetTooltip(target, tooltip, delaySeconds);
		return tooltip;
	}

	UiModal CreateModal(UiDocument& document, std::string title)
	{
		UiModal modal;
		UiStyle scrim;
		scrim.Visible = false;
		scrim.Absolute = true;
		scrim.HitTest = true; // Presses on the page below land here.
		scrim.Width = UiLength::Percent(1.0f);
		scrim.Height = UiLength::Percent(1.0f);
		scrim.Flow = UiFlow::Column;
		scrim.Justify = UiJustify::Center;
		scrim.AlignItems = UiAlign::Center;
		modal.Root = Styled(document, document.GetRoot(), scrim);
		document.SetThemeClass(modal.Root, UiThemeClass::ModalScrim, UiThemeApply::Paint);
		UiStyle dialog;
		dialog.Flow = UiFlow::Column;
		dialog.AlignItems = UiAlign::Stretch;
		modal.Dialog = Styled(document, modal.Root, dialog);
		document.SetThemeClass(modal.Dialog, UiThemeClass::Dialog);
		if (!title.empty())
		{
			modal.Title = document.Create(modal.Dialog);
			document.SetText(modal.Title, Fonts(document), std::move(title), TextSize(document, UiThemeClass::DialogTitle));
			document.SetThemeClass(modal.Title, UiThemeClass::DialogTitle);
		}
		UiStyle content;
		content.Flow = UiFlow::Column;
		content.Gap = document.GetTheme()->Metrics.Spacing;
		modal.Content = Styled(document, modal.Dialog, content);
		UiStyle buttons;
		buttons.Flow = UiFlow::Row;
		buttons.Justify = UiJustify::End;
		buttons.Gap = document.GetTheme()->Metrics.Spacing;
		modal.Buttons = Styled(document, modal.Dialog, buttons);
		return modal;
	}

	UiNodeId AddModalButton(UiDocument& document, const UiModal& modal, std::string label)
	{
		return CreateButton(document, modal.Buttons, std::move(label));
	}

	void OpenModal(UiDocument& document, const UiModal& modal)
	{
		UiPopupDesc desc;
		desc.Side = UiPopupSide::Center;
		desc.Modal = true;
		desc.LightDismiss = false;
		document.OpenPopup(modal.Root, desc);
	}

	UiVirtualList::UiVirtualList(UiDocument& documentInput, UiNodeId parent, UiVirtualListDesc descInput)
		: document(documentInput), desc(std::move(descInput))
	{
		if (desc.ItemHeight == 0.0f)
		{
			desc.ItemHeight = document.GetTheme()->Metrics.ControlHeight;
		}
		if (!std::isfinite(desc.ItemHeight) || desc.ItemHeight <= 0.0f ||
			double(desc.ItemCount) * double(desc.ItemHeight) > double(1000000.0f) || desc.Overscan > 1024)
		{
			throw std::invalid_argument("A virtual list needs a positive item height, at most 1,000,000 units long in total");
		}
		list = CreateListView(document, parent, desc.Style);
		UiStyle contentStyle;
		contentStyle.AlignSelf = UiAlign::Stretch;
		contentStyle.Height = UiLength::Pixels(float(desc.ItemCount) * desc.ItemHeight);
		content = Styled(document, list.Viewport, contentStyle);
		auto control = document.GetControl(list.Root);
		control.ItemCount = desc.ItemCount;
		control.ItemExtent = desc.ItemHeight;
		control.Value = desc.ItemCount > 0 ? float(std::clamp<std::int64_t>(desc.Selected, -1, std::int64_t(desc.ItemCount) - 1)) : -1.0f;
		document.SetControl(list.Root, control);
	}

	void UiVirtualList::SetItemCount(std::uint32_t count)
	{
		if (double(count) * double(desc.ItemHeight) > double(1000000.0f))
		{
			throw std::invalid_argument("A virtual list is at most 1,000,000 units long");
		}
		desc.ItemCount = count;
		auto contentStyle = document.GetStyle(content);
		contentStyle.Height = UiLength::Pixels(float(count) * desc.ItemHeight);
		document.SetStyle(content, contentStyle);
		auto control = document.GetControl(list.Root);
		control.ItemCount = count;
		control.Value = count > 0 ? std::min(control.Value, float(count) - 1.0f) : -1.0f;
		document.SetControl(list.Root, control);
		Refresh();
	}

	void UiVirtualList::Refresh()
	{
		for (auto& row : rows)
		{
			row.Index = -1;
		}
	}

	bool UiVirtualList::Update()
	{
		if (document.IsLayoutCurrent())
		{
			viewport = document.GetBounds(list.Viewport).Height;
		}
		const float extent = desc.ItemHeight;
		const float total = float(desc.ItemCount) * extent;
		const float scroll = std::clamp(document.GetScroll(list.Viewport).Y, 0.0f, std::max(0.0f, total - viewport));
		const auto count = std::int64_t(desc.ItemCount);
		const auto first = std::max<std::int64_t>(0, std::int64_t(std::floor(scroll / extent)) - desc.Overscan);
		const auto last = std::min<std::int64_t>(count, std::int64_t(std::ceil((scroll + viewport) / extent)) + desc.Overscan);
		bool changed = false;
		std::vector<std::size_t> free;
		std::vector<bool> shown(std::size_t(std::max<std::int64_t>(0, last - first)), false);
		for (std::size_t i = 0; i < rows.size(); ++i)
		{
			const auto index = rows[i].Index;
			if (index >= first && index < last && !shown[std::size_t(index - first)])
			{
				shown[std::size_t(index - first)] = true;
			}
			else
			{
				free.push_back(i);
			}
		}
		for (auto index = first; index < last; ++index)
		{
			if (shown[std::size_t(index - first)])
			{
				continue;
			}
			std::size_t slot = 0;
			if (!free.empty())
			{
				slot = free.back();
				free.pop_back();
			}
			else
			{
				UiStyle rowStyle;
				rowStyle.Absolute = true;
				rowStyle.AnchorMin = { 0.0f, 0.0f };
				rowStyle.AnchorMax = { 1.0f, 0.0f };
				rowStyle.Padding = document.GetTheme()->Metrics.MenuItemPadding;
				rowStyle.TextWrap = Text::TextWrap::None;
				rows.push_back({ Styled(document, content, rowStyle), -1 });
				document.SetThemeClass(rows.back().Node, UiThemeClass::MenuItem, UiThemeApply::Paint | UiThemeApply::Text);
				slot = rows.size() - 1;
			}
			auto& row = rows[slot];
			auto style = document.GetStyle(row.Node);
			style.Visible = true;
			style.Height = UiLength::Pixels(extent);
			style.Offset = { 0.0f, float(index) * extent };
			document.SetStyle(row.Node, style);
			document.SetPartRole(row.Node, list.Root, UiPartRole::Option, float(index));
			row.Index = index;
			if (desc.Bind)
			{
				desc.Bind(document, row.Node, std::uint32_t(index));
			}
			++binds;
			changed = true;
		}
		for (const auto slot : free)
		{
			auto& row = rows[slot];
			if (document.GetStyle(row.Node).Visible || row.Index >= 0)
			{
				auto style = document.GetStyle(row.Node);
				style.Visible = false;
				document.SetStyle(row.Node, style);
				document.SetPartRole(row.Node, {}, UiPartRole::None);
				row.Index = -1;
				changed = true;
			}
		}
		return changed;
	}

	std::uint32_t UiVirtualList::GetBoundRowCount() const
	{
		return std::uint32_t(std::count_if(rows.begin(), rows.end(),
			[](const Row& row)
			{
				return row.Index >= 0;
			}));
	}

	UiNodeId UiVirtualList::FindRow(std::uint32_t index) const
	{
		for (const auto& row : rows)
		{
			if (row.Index == std::int64_t(index))
			{
				return row.Node;
			}
		}
		return {};
	}
} // namespace Swim::UI
