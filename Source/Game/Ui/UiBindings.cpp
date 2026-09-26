#include "Game/Ui/UiBindings.h"

#include "Engine/Systems/UI/UiTheme.h"
#include "Engine/Systems/UI/UiWidgets.h"

namespace Game
{
	using namespace Swim::UI;

	void UiBindings::OnClick(UiNodeId node, std::function<void()> handler)
	{
		clickHandlers[node.Value] = std::move(handler);
	}

	void UiBindings::OnValue(UiNodeId node, std::function<void(float)> handler)
	{
		values.push_back({ node, std::move(handler), 0.0f, false });
	}

	void UiBindings::OnChecked(UiNodeId node, std::function<void(bool)> handler)
	{
		checks.push_back({ node, std::move(handler), false, false });
	}

	void UiBindings::OnText(UiNodeId node, std::function<void(const std::string&)> handler)
	{
		texts.push_back({ node, std::move(handler), {}, false });
	}

	void UiBindings::Clear()
	{
		clickHandlers.clear();
		values.clear();
		checks.clear();
		texts.clear();
	}

	void UiBindings::Process(UiDocument& document)
	{
		for (const auto& event : document.DrainEvents())
		{
			if (event.Kind != UiEventKind::Click)
			{
				continue;
			}
			const auto found = clickHandlers.find(event.Node.Value);
			if (found != clickHandlers.end() && found->second)
			{
				++clicks;
				found->second();
			}
		}
		for (auto& watch : values)
		{
			if (!document.Contains(watch.Node))
			{
				continue;
			}
			const float value = document.GetValue(watch.Node);
			if (!watch.Seen)
			{
				// The first observation is the control's initial state, not a change.
				watch.Seen = true;
				watch.Last = value;
			}
			else if (value != watch.Last)
			{
				watch.Last = value;
				watch.Handler(value);
			}
		}
		for (auto& watch : checks)
		{
			if (!document.Contains(watch.Node))
			{
				continue;
			}
			const bool checked = document.GetChecked(watch.Node) == UiCheckState::Checked;
			if (!watch.Seen)
			{
				// The first observation is the control's initial state, not a change.
				watch.Seen = true;
				watch.Last = checked;
			}
			else if (checked != watch.Last)
			{
				watch.Last = checked;
				watch.Handler(checked);
			}
		}
		for (auto& watch : texts)
		{
			if (!document.Contains(watch.Node))
			{
				continue;
			}
			const std::string& text = document.GetText(watch.Node);
			if (!watch.Seen)
			{
				// The first observation is the control's initial state, not a change.
				watch.Seen = true;
				watch.Last = text;
			}
			else if (text != watch.Last)
			{
				watch.Last = text;
				watch.Handler(text);
			}
		}
	}

	void SetLabelText(UiDocument& document, UiNodeId node, const std::string& text)
	{
		const auto& theme = document.GetTheme();
		if (!theme)
		{
			return;
		}
		SetLabelText(document, node, text, theme->Fonts, theme->Class(UiThemeClass::Label).TextSize);
	}

	void SetLabelText(UiDocument& document, UiNodeId node, const std::string& text,
		const std::shared_ptr<const Swim::Text::FontCollection>& fonts, float size)
	{
		if (!document.Contains(node) || !fonts || document.GetText(node) == text)
		{
			return;
		}
		document.SetText(node, fonts, text, size);
	}

	UiNodeId CreateStyledNode(UiDocument& document, UiNodeId parent, const UiStyle& style, UiThemeClass paintClass)
	{
		const auto node = document.Create(parent);
		if (paintClass != UiThemeClass::None)
		{
			document.SetThemeClass(node, paintClass, UiThemeApply::Paint);
		}
		document.SetStyle(node, style);
		return node;
	}

	UiNodeId CreateHeading(UiDocument& document, UiNodeId parent, const std::string& text)
	{
		const auto label = CreateLabel(document, parent, text);
		auto style = document.GetStyle(label);
		style.TextColor = { 0.55f, 0.75f, 1.0f, 1.0f };
		style.Margin.Top = 6.0f;
		document.SetStyle(label, style);
		return label;
	}

	UiNodeId CreateRow(UiDocument& document, UiNodeId parent, float gap)
	{
		UiStyle style;
		style.Flow = UiFlow::Row;
		style.Gap = gap;
		style.AlignItems = UiAlign::Center;
		return CreateStyledNode(document, parent, style);
	}
} // namespace Game
