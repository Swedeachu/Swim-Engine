#include "Engine/Systems/UI/Widgets/UiTooltip.h"

#include "Engine/Systems/UI/Internal/UiWidgetHelpers.h"

namespace Swim::UI
{

	UiNodeId CreateTooltip(UiDocument& document, UiNodeId target, std::string text, float delaySeconds)
	{
		UiStyle style;
		style.Visible = false;
		style.Absolute = true;
		style.TextWrap = Text::TextWrap::None;
		const auto tooltip = Internal::CreateStyled(document, document.GetRoot(), style);
		document.SetText(tooltip, Internal::ThemeFonts(document), std::move(text), Internal::ThemeTextSize(document, UiThemeClass::Tooltip));
		document.SetThemeClass(tooltip, UiThemeClass::Tooltip);
		document.SetTooltip(target, tooltip, delaySeconds);
		return tooltip;
	}

} // namespace Swim::UI
