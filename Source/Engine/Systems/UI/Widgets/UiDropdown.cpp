#include "Engine/Systems/UI/Widgets/UiDropdown.h"

#include "Engine/Systems/UI/Internal/UiBuiltInControls.h"
#include "Engine/Systems/UI/Internal/UiWidgetHelpers.h"
#include "Engine/Systems/UI/UiControlRegistry.h"

#include <algorithm>
#include <deque>

namespace Swim::UI
{

	bool UiDropdownControl::IsOpen(const UiControlContext& context) const
	{
		return context.IsPopupOpen(context.Control().Parts.Popup);
	}

	void UiDropdownControl::OnAttached(UiControlContext& context)
	{
		highlight = -1;
		ShowSelection(context);
	}

	void UiDropdownControl::OnValueChanged(UiControlContext& context)
	{
		ShowSelection(context);
	}

	void UiDropdownControl::ShowSelection(UiControlContext& context) const
	{
		const auto label = context.Control().Parts.Label;

		if (!label || !context.Contains(label))
		{
			return;
		}

		const auto option = context.FindOption(GetSelected(context));

		if (!context.HasFonts(label) || !option)
		{
			return; // No choice (or an unbound virtual row): the label keeps its text (a placeholder).
		}

		// The option's own text, else its first descendant with text (breadth first).
		const std::string* text = nullptr;
		std::deque<UiNodeId> pending{ option };

		while (!pending.empty())
		{
			const auto id = pending.front();
			pending.pop_front();

			if (!context.GetText(id).empty())
			{
				text = &context.GetText(id);
				break;
			}

			const auto& children = context.GetChildren(id);
			pending.insert(pending.end(), children.begin(), children.end());
		}

		if (text)
		{
			context.SetText(label, *text);
		}
	}

	void UiDropdownControl::Toggle(UiControlContext& context)
	{
		const auto& c = context.Control();
		const auto popup = c.Parts.Popup;

		if (!popup || !context.Contains(popup))
		{
			return;
		}

		if (IsOpen(context))
		{
			context.ClosePopup(popup);
			return;
		}

		if (c.ReadOnly)
		{
			return;
		}

		UiPopupDesc desc;
		desc.Anchor = context.GetNode();
		desc.Side = UiPopupSide::Below;
		desc.MatchAnchorWidth = true;
		desc.FocusFirst = false; // Focus stays on the dropdown; keys move the highlight.
		context.OpenPopup(popup, desc);
		highlight = std::max(0, GetSelected(context));
		context.InvalidateVisuals(popup);
		Reveal(context, highlight);
	}

	void UiDropdownControl::OnPointerUp(UiControlContext& context, bool inside, bool)
	{
		if (inside)
		{
			Toggle(context);
		}
	}

	UiActivation UiDropdownControl::OnActivate(UiControlContext& context)
	{
		Toggle(context);
		return UiActivation::KeepOpen;
	}

	void UiDropdownControl::OnBlur(UiControlContext& context, UiNodeId newFocus)
	{
		// Focus leaving closes the list, unless it moves into the list.
		const auto popup = context.Control().Parts.Popup;

		if (IsOpen(context) && !(newFocus && (newFocus == popup || context.IsInside(newFocus, popup))))
		{
			Toggle(context);
		}
	}

	void UiDropdownControl::OnPopupLaidOut(UiControlContext& context, UiNodeId)
	{
		Reveal(context, highlight); // Now that the options have bounds.
	}

	bool UiDropdownControl::OnKey(UiControlContext& context, UiKey key, UiKeyModifiers)
	{
		const auto& c = context.Control();

		if (c.ReadOnly)
		{
			return false;
		}

		const auto count = static_cast<std::int32_t>(context.GetOptionCount());
		const bool open = IsOpen(context);

		if (key == UiKey::Enter || key == UiKey::Space)
		{
			if (open && highlight >= 0 && highlight < count)
			{
				Select(context, highlight, true);
			}

			Toggle(context);
			return true;
		}

		std::int32_t next = open ? highlight : GetSelected(context);

		switch (key)
		{
		case UiKey::Up: next = next < 0 ? 0 : next - 1; break;
		case UiKey::Down: next = next < 0 ? 0 : next + 1; break;
		case UiKey::Home: next = 0; break;
		case UiKey::End: next = count - 1; break;
		default: return false;
		}

		if (count == 0)
		{
			return true;
		}

		next = std::clamp(next, 0, count - 1);

		if (open)
		{
			if (next != highlight)
			{
				highlight = next;

				if (c.Parts.Popup && context.Contains(c.Parts.Popup))
				{
					context.InvalidateVisuals(c.Parts.Popup);
				}
			}

			Reveal(context, next);
			return true;
		}

		Select(context, next, true);
		return true;
	}

	void UiDropdownControl::OnOptionPressed(UiControlContext& context, std::int32_t index)
	{
		if (!context.IsAvailable())
		{
			return;
		}

		Select(context, index, true);

		if (IsOpen(context))
		{
			Toggle(context); // Closes.
		}
	}

	void UiDropdownControl::OnOptionHovered(UiControlContext& context, std::int32_t index)
	{
		// Hovering an option of the open list moves the highlight.
		if (IsOpen(context))
		{
			highlight = index;
			context.InvalidateVisuals(context.Control().Parts.Popup);
		}
	}

	UiState UiDropdownControl::GetOptionState(const UiControlContext& context, std::int32_t index) const
	{
		if (!IsOpen(context))
		{
			return UiSelectionControl::GetOptionState(context, index);
		}

		// Open: the highlight is what keys move (Focused), whatever has focus.
		auto state = UiSelectionControl::GetOptionState(context, index);
		state = static_cast<UiState>(static_cast<std::uint16_t>(state) & ~static_cast<std::uint16_t>(UiState::Focused));
		return highlight == index ? state | UiState::Focused : state;
	}

	UiDropdown CreateDropdown(
		UiDocument& document, UiNodeId parent, const std::vector<std::string>& options, std::int32_t selected, std::string placeholder)
	{
		UiDropdown dropdown;
		UiStyle row;
		row.Flow = UiFlow::Row;
		row.AlignItems = UiAlign::Center;
		dropdown.Root = Internal::CreateStyled(document, parent, row);
		UiStyle labelStyle;
		labelStyle.Grow = 1.0f;
		labelStyle.Shrink = 1.0f;
		labelStyle.Clip = true;
		labelStyle.TextWrap = Text::TextWrap::None;
		dropdown.Label = Internal::CreateStyled(document, dropdown.Root, labelStyle);
		document.SetText(
			dropdown.Label, Internal::ThemeFonts(document), std::move(placeholder), Internal::ThemeTextSize(document, UiThemeClass::Dropdown));
		dropdown.Arrow = document.Create(dropdown.Root);
		dropdown.List = CreatePopupList(document);
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
		return CreateTextOption(document, dropdown.List.Items, dropdown.Root, std::move(text));
	}

	void Internal::RegisterDropdown(UiControlRegistry& registry)
	{
		registry.Register<UiDropdownControl>("Dropdown",
			[](UiDocument& document, UiNodeId parent)
			{
				return CreateDropdown(document, parent, std::vector<std::string>{}).Root;
			});
	}

} // namespace Swim::UI
