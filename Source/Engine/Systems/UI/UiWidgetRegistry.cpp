#include "Engine/Systems/UI/UiWidgetRegistry.h"
#include "Engine/Systems/UI/UiWidgets.h"
#include "Engine/Systems/UI/Internal/UiWidgetBuilders.h"

namespace Swim::UI
{

	UiWidgetRegistry::UiWidgetRegistry()
	{
		Register<UiPanelDesc, UiNodeId>("Panel",
			[](UiDocument& document, UiNodeId parent, const UiPanelDesc& o)
			{
				return Internal::BuildPanel(document, parent, o.Flow);
			});

		Register<UiLabelDesc, UiNodeId>("Label",
			[](UiDocument& document, UiNodeId parent, const UiLabelDesc& o)
			{
				if (!o.Themed)
				{
					return Internal::BuildLabel(document, parent, o.Fonts, o.Text, o.Size, o.Style);
				}

				return Internal::BuildLabel(document, parent, o.Text);
			});

		Register<UiButtonDesc, UiNodeId>("Button",
			[](UiDocument& document, UiNodeId parent, const UiButtonDesc& o)
			{
				return Internal::BuildButton(document, parent, o.Label);
			});

		Register<UiTextFieldDesc, UiNodeId>("TextField",
			[](UiDocument& document, UiNodeId parent, const UiTextFieldDesc& o)
			{
				if (!o.Themed)
				{
					return Internal::BuildTextField(document, parent, o.Fonts, o.Size, o.Options, o.Style);
				}

				return Internal::BuildTextField(document, parent, o.Options);
			});

		Register<UiCheckboxDesc, UiNodeId>("Checkbox",
			[](UiDocument& document, UiNodeId parent, const UiCheckboxDesc& o)
			{
				return Internal::BuildCheckbox(document, parent, o.Label, o.State);
			});

		Register<UiToggleDesc, UiNodeId>("Toggle",
			[](UiDocument& document, UiNodeId parent, const UiToggleDesc& o)
			{
				return Internal::BuildToggle(document, parent, o.Label, o.On);
			});

		Register<UiSliderDesc, UiNodeId>("Slider",
			[](UiDocument& document, UiNodeId parent, const UiSliderDesc& o)
			{
				return Internal::BuildSlider(document, parent, o);
			});

		Register<UiScrollBarWidgetDesc, UiNodeId>("ScrollBar",
			[](UiDocument& document, UiNodeId parent, const UiScrollBarWidgetDesc& o)
			{
				return Internal::BuildScrollBar(document, parent, o.Target, o.Options);
			});

		Register<UiScrollAreaDesc, UiScrollArea>("ScrollArea",
			[](UiDocument& document, UiNodeId parent, const UiScrollAreaDesc& o)
			{
				return Internal::BuildScrollArea(document, parent, o.Style, o.Vertical, o.Horizontal, o.Visibility, o.StepButtons);
			});

		Register<UiImageDesc, UiNodeId>("Image",
			[](UiDocument& document, UiNodeId parent, const UiImageDesc& o)
			{
				return Internal::BuildImage(document, parent, o.Image, o.Style);
			});

		Register<UiStyle, UiNodeId>("ScrollView",
			[](UiDocument& document, UiNodeId parent, const UiStyle& o)
			{
				return Internal::BuildScrollView(document, parent, o);
			});

		Register<UiRadioGroupDesc, UiNodeId>("RadioGroup",
			[](UiDocument& document, UiNodeId parent, const UiRadioGroupDesc& o)
			{
				return Internal::BuildRadioGroup(document, parent, o.Options, o.Selected, o.Orientation);
			});

		Register<UiListViewDesc, UiListView>("ListView",
			[](UiDocument& document, UiNodeId parent, const UiListViewDesc& o)
			{
				return Internal::BuildListView(document, parent, o.Style, o.Items, o.Selected);
			});

		Register<UiDropdownDesc, UiDropdown>("Dropdown",
			[](UiDocument& document, UiNodeId parent, const UiDropdownDesc& o)
			{
				return Internal::BuildDropdown(document, parent, o.Options, o.Selected, o.Placeholder);
			});

		Register<UiMenuDesc, UiPopupList>("Menu",
			[](UiDocument& document, UiNodeId parent, const UiMenuDesc& o)
			{
				(void)parent;
				(void)o;
				return Internal::BuildMenu(document);
			});

		Register<UiTooltipDesc, UiNodeId>("Tooltip",
			[](UiDocument& document, UiNodeId parent, const UiTooltipDesc& o)
			{
				return Internal::BuildTooltip(document, parent, o.Text, o.DelaySeconds);
			});

		Register<UiModalDesc, UiModal>("Modal",
			[](UiDocument& document, UiNodeId parent, const UiModalDesc& o)
			{
				(void)parent;
				return Internal::BuildModal(document, o.Title);
			});
	}

} // namespace Swim::UI
