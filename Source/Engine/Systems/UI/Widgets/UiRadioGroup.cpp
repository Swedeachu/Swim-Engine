#include "Engine/Systems/UI/Widgets/UiRadioGroup.h"

#include "Engine/Systems/UI/Internal/UiBuiltInControls.h"
#include "Engine/Systems/UI/Internal/UiWidgetHelpers.h"
#include "Engine/Systems/UI/UiControlRegistry.h"

namespace Swim::UI
{

	bool UiRadioGroupControl::OnKey(UiControlContext& context, UiKey key, UiKeyModifiers)
	{
		if (context.Control().ReadOnly)
		{
			return false;
		}

		const auto count = static_cast<std::int32_t>(context.GetOptionCount());
		const auto value = GetSelected(context);
		std::int32_t delta = 0;

		switch (key)
		{
		case UiKey::Left:
		case UiKey::Up:
			delta = -1;
			break;
		case UiKey::Right:
		case UiKey::Down:
			delta = 1;
			break;
		case UiKey::Home:
			Select(context, 0, true);
			return true;
		case UiKey::End:
			Select(context, count - 1, true);
			return true;
		case UiKey::Space:
		case UiKey::Enter:

			if (value < 0 && count > 0)
			{
				Select(context, 0, true);
				return true;
			}

			return false; // Activation (a Click).
		default:
			return false;
		}

		if (count > 0)
		{
			// Wraps around, like platform radio groups.
			const auto next = value < 0 ? (delta > 0 ? 0 : count - 1) : (value + delta + count) % count;
			Select(context, next, true);
		}

		return true;
	}

	UiNodeId CreateRadioGroup(
		UiDocument& document, UiNodeId parent, const std::vector<std::string>& options, std::int32_t selected, UiOrientation orientation)
	{
		UiStyle style;
		style.Flow = orientation == UiOrientation::Horizontal ? UiFlow::Row : UiFlow::Column;
		style.Gap = Internal::ThemeOf(document).Metrics.Spacing * (orientation == UiOrientation::Horizontal ? 1.5f : 0.5f);
		const auto group = Internal::CreateStyled(document, parent, style);
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
		const auto row = Internal::CreateStyled(document, group, rowStyle);
		UiStyle overlay;
		overlay.Flow = UiFlow::Overlay;
		overlay.AlignItems = UiAlign::Center;
		const auto circle = Internal::CreateStyled(document, row, overlay);
		const auto dot = document.Create(circle);

		if (!label.empty())
		{
			const auto text = document.Create(row);
			document.SetText(text, Internal::ThemeFonts(document), std::move(label), Internal::ThemeTextSize(document, UiThemeClass::Label));
			document.SetThemeClass(text, UiThemeClass::Label);
		}

		document.SetPartRole(row, group, UiPartRole::Option, float(document.GetOptionCount(group)));
		document.SetThemeClass(row, UiThemeClass::RadioOption);
		document.SetThemeClass(circle, UiThemeClass::RadioCircle);
		document.SetThemeClass(dot, UiThemeClass::RadioDot);
		return row;
	}

	void Internal::RegisterRadioGroup(UiControlRegistry& registry)
	{
		registry.Register<UiRadioGroupControl>("RadioGroup",
			[](UiDocument& document, UiNodeId parent)
			{
				return CreateRadioGroup(document, parent, std::vector<std::string>{});
			});
	}

} // namespace Swim::UI
