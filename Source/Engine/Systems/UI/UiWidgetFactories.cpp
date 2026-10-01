#include "Engine/Systems/UI/UiWidgets.h"

namespace Swim::UI
{

	UiNodeId CreatePanel(UiDocument& document, UiNodeId parent, UiFlow flow)
	{
		return document.GetWidgets().Create<UiNodeId>("Panel", document, parent, UiPanelDesc{ flow });
	}

	UiNodeId CreateLabel(UiDocument& document, UiNodeId parent, std::string text)
	{
		return document.GetWidgets().Create<UiNodeId>("Label", document, parent, UiLabelDesc{ std::move(text), {}, 16.0f, {} });
	}

	UiNodeId CreateButton(UiDocument& document, UiNodeId parent, std::string label)
	{
		return document.GetWidgets().Create<UiNodeId>("Button", document, parent, UiButtonDesc{ std::move(label) });
	}

	UiNodeId CreateTextField(UiDocument& document, UiNodeId parent, const UiTextEditOptions& options)
	{
		return document.GetWidgets().Create<UiNodeId>("TextField", document, parent, UiTextFieldDesc{ options, {}, 16.0f, {} });
	}

	UiNodeId CreateCheckbox(UiDocument& document, UiNodeId parent, std::string label, UiCheckState state)
	{
		return document.GetWidgets().Create<UiNodeId>("Checkbox", document, parent, UiCheckboxDesc{ std::move(label), state });
	}

	UiNodeId CreateToggle(UiDocument& document, UiNodeId parent, std::string label, bool on)
	{
		return document.GetWidgets().Create<UiNodeId>("Toggle", document, parent, UiToggleDesc{ std::move(label), on });
	}

	UiNodeId CreateSlider(UiDocument& document, UiNodeId parent, const UiSliderDesc& desc)
	{
		return document.GetWidgets().Create<UiNodeId>("Slider", document, parent, desc);
	}

	UiNodeId CreateScrollBar(UiDocument& document, UiNodeId parent, UiNodeId target, const UiScrollBarDesc& desc)
	{
		return document.GetWidgets().Create<UiNodeId>("ScrollBar", document, parent, UiScrollBarWidgetDesc{ target, desc });
	}

	UiScrollArea CreateScrollArea(UiDocument& document, UiNodeId parent, const UiStyle& style, bool vertical, bool horizontal,
		UiScrollBarVisibility visibility, bool stepButtons)
	{
		return document.GetWidgets().Create<UiScrollArea>(
			"ScrollArea", document, parent, UiScrollAreaDesc{ style, vertical, horizontal, visibility, stepButtons });
	}

	UiNodeId CreateLabel(UiDocument& document, UiNodeId parent, std::shared_ptr<const Text::FontCollection> fonts, std::string text,
		float size, const UiStyle& style)
	{
		return document.GetWidgets().Create<UiNodeId>(
			"Label", document, parent, UiLabelDesc{ std::move(text), std::move(fonts), size, style, false });
	}

	UiNodeId CreateImage(UiDocument& document, UiNodeId parent, const UiImage& image, const UiStyle& style)
	{
		return document.GetWidgets().Create<UiNodeId>("Image", document, parent, UiImageDesc{ image, style });
	}

	UiNodeId CreateScrollView(UiDocument& document, UiNodeId parent, const UiStyle& style)
	{
		return document.GetWidgets().Create<UiNodeId>("ScrollView", document, parent, style);
	}

	UiNodeId CreateTextField(UiDocument& document, UiNodeId parent, std::shared_ptr<const Text::FontCollection> fonts, float size,
		const UiTextEditOptions& options, const UiStyle& style)
	{
		return document.GetWidgets().Create<UiNodeId>(
			"TextField", document, parent, UiTextFieldDesc{ options, std::move(fonts), size, style, false });
	}

	UiNodeId CreateRadioGroup(
		UiDocument& document, UiNodeId parent, const std::vector<std::string>& options, std::int32_t selected, UiOrientation orientation)
	{
		return document.GetWidgets().Create<UiNodeId>("RadioGroup", document, parent, UiRadioGroupDesc{ options, selected, orientation });
	}

	UiListView CreateListView(
		UiDocument& document, UiNodeId parent, const UiStyle& style, const std::vector<std::string>& items, std::int32_t selected)
	{
		return document.GetWidgets().Create<UiListView>("ListView", document, parent, UiListViewDesc{ style, items, selected });
	}

	UiDropdown CreateDropdown(
		UiDocument& document, UiNodeId parent, const std::vector<std::string>& options, std::int32_t selected, std::string placeholder)
	{
		return document.GetWidgets().Create<UiDropdown>(
			"Dropdown", document, parent, UiDropdownDesc{ options, selected, std::move(placeholder) });
	}

	UiPopupList CreateMenu(UiDocument& document)
	{
		return document.GetWidgets().Create<UiPopupList>("Menu", document, document.GetRoot(), UiMenuDesc{});
	}

	UiNodeId CreateTooltip(UiDocument& document, UiNodeId target, std::string text, float delaySeconds)
	{
		return document.GetWidgets().Create<UiNodeId>("Tooltip", document, target, UiTooltipDesc{ std::move(text), delaySeconds });
	}

	UiModal CreateModal(UiDocument& document, std::string title)
	{
		return document.GetWidgets().Create<UiModal>("Modal", document, document.GetRoot(), UiModalDesc{ std::move(title) });
	}

} // namespace Swim::UI
