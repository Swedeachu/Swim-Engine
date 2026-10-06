#include "Engine/Systems/UI/Widgets/UiButton.h"

#include "Engine/Systems/UI/Internal/UiBuiltInControls.h"
#include "Engine/Systems/UI/Internal/UiWidgetHelpers.h"
#include "Engine/Systems/UI/UiControlRegistry.h"

#include <stdexcept>

namespace Swim::UI
{

	void UiButtonControl::SetValue(UiControlContext&, float)
	{
		throw std::invalid_argument("SetValue needs a slider, scroll bar, checkbox, toggle or selection control");
	}

	UiNodeId CreateButton(UiDocument& document, UiNodeId parent, std::string label)
	{
		UiStyle style;
		style.TextAlign = Text::TextAlign::Center;
		const auto node = Internal::CreateStyled(document, parent, style);
		document.SetText(node, Internal::ThemeFonts(document), std::move(label), Internal::ThemeTextSize(document, UiThemeClass::Button));
		UiControl control;
		control.Kind = UiControlKind::Button;
		document.SetControl(node, control);
		document.SetThemeClass(node, UiThemeClass::Button);
		return node;
	}

	void Internal::RegisterButton(UiControlRegistry& registry)
	{
		registry.Register<UiButtonControl>("Button",
			[](UiDocument& document, UiNodeId parent)
			{
				return CreateButton(document, parent, "Button");
			});
	}

} // namespace Swim::UI
