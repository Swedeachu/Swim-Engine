#include "Engine/Systems/UI/UiWidgets.h"

namespace Swim::UI
{
	UiNodeId CreateLabel(UiDocument& document, UiNodeId parent, std::shared_ptr<const Text::FontCollection> fonts, std::string text,
		float size, const UiStyle& style)
	{
		const auto node = document.Create(parent);
		document.SetStyle(node, style);
		document.SetText(node, std::move(fonts), std::move(text), size);
		return node;
	}

	UiStyle DefaultButtonStyle(const UiButtonColors& colors)
	{
		UiStyle style;
		style.Padding = { 12.0f, 6.0f, 12.0f, 6.0f };
		style.HitTest = true;
		style.Focusable = true;
		style.Background = colors.Normal;
		style.CornerRadius = 6.0f;
		style.TextAlign = Text::TextAlign::Center;
		return style;
	}

	UiNodeId CreateButton(UiDocument& document, UiNodeId parent, std::shared_ptr<const Text::FontCollection> fonts, std::string label,
		float size, const UiStyle& style)
	{
		return CreateLabel(document, parent, std::move(fonts), std::move(label), size, style);
	}

	UiNodeId CreateImage(UiDocument& document, UiNodeId parent, const UiImage& image, const UiStyle& style)
	{
		const auto node = document.Create(parent);
		document.SetStyle(node, style);
		document.SetImage(node, image);
		return node;
	}

	UiNodeId CreateScrollView(UiDocument& document, UiNodeId parent, const UiStyle& style)
	{
		auto clipped = style;
		clipped.Clip = true;
		const auto node = document.Create(parent);
		document.SetStyle(node, clipped);
		return node;
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
		const auto node = document.Create(parent);
		document.SetStyle(node, field);
		document.SetText(node, std::move(fonts), {}, size);
		document.SetEditable(node, true, options);
		return node;
	}

	void UiButtonStates::Track(UiNodeId button, const UiButtonColors& colors)
	{
		buttons[button.Value] = State{ colors };
	}

	void UiButtonStates::Untrack(UiNodeId button)
	{
		buttons.erase(button.Value);
	}

	void UiButtonStates::Refresh(UiDocument& document, UiNodeId button, const State& state) const
	{
		auto style = document.GetStyle(button);
		style.Background = state.Pressed ? state.Colors.Pressed : state.Hovered ? state.Colors.Hover : state.Colors.Normal;
		style.BorderWidth = state.Focused ? state.Colors.FocusBorder : 0.0f;
		style.BorderColor = state.Colors.Focus;
		document.SetStyle(button, style); // Paint-only: no relayout.
	}

	void UiButtonStates::Apply(UiDocument& document, std::span<const UiEvent> events)
	{
		for (const auto& event : events)
		{
			const auto found = buttons.find(event.Node.Value);
			if (found == buttons.end())
			{
				continue;
			}
			if (!document.Contains(event.Node))
			{
				buttons.erase(found);
				continue;
			}
			auto& state = found->second;
			switch (event.Kind)
			{
			case UiEventKind::Enter:
				state.Hovered = true;
				break;
			case UiEventKind::Leave:
				state.Hovered = false;
				break;
			case UiEventKind::Press:
				state.Pressed = true;
				break;
			case UiEventKind::Release:
			case UiEventKind::Cancel:
				state.Pressed = false;
				break;
			case UiEventKind::Focus:
				state.Focused = true;
				break;
			case UiEventKind::Blur:
				state.Focused = false;
				break;
			default:
				continue;
			}
			Refresh(document, event.Node, state);
		}
	}
} // namespace Swim::UI
