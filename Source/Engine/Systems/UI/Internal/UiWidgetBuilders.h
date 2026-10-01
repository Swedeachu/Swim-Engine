#pragma once

#include "Engine/Systems/UI/UiWidgets.h"

namespace Swim::UI::Internal
{

	UiNodeId BuildPanel(UiDocument& document, UiNodeId parent, UiFlow flow);

	UiNodeId BuildLabel(UiDocument& document, UiNodeId parent, std::string text);

	UiNodeId BuildButton(UiDocument& document, UiNodeId parent, std::string label);

	UiNodeId BuildTextField(UiDocument& document, UiNodeId parent, const UiTextEditOptions& options);

	UiNodeId BuildCheckbox(UiDocument& document, UiNodeId parent, std::string label, UiCheckState state);

	UiNodeId BuildToggle(UiDocument& document, UiNodeId parent, std::string label, bool on);

	UiNodeId BuildSlider(UiDocument& document, UiNodeId parent, const UiSliderDesc& desc);

	UiNodeId BuildScrollBar(UiDocument& document, UiNodeId parent, UiNodeId target, const UiScrollBarDesc& desc);

	UiScrollArea BuildScrollArea(UiDocument& document, UiNodeId parent, const UiStyle& rootStyle, bool vertical, bool horizontal,
		UiScrollBarVisibility visibility, bool stepButtons);

	UiNodeId BuildLabel(UiDocument& document, UiNodeId parent, std::shared_ptr<const Text::FontCollection> fonts, std::string text,
		float size, const UiStyle& style);

	UiNodeId BuildImage(UiDocument& document, UiNodeId parent, const UiImage& image, const UiStyle& style);

	UiNodeId BuildScrollView(UiDocument& document, UiNodeId parent, const UiStyle& style);

	UiNodeId BuildTextField(UiDocument& document, UiNodeId parent, std::shared_ptr<const Text::FontCollection> fonts, float size,
		const UiTextEditOptions& options, const UiStyle& style);

	UiNodeId BuildRadioGroup(
		UiDocument& document, UiNodeId parent, const std::vector<std::string>& options, std::int32_t selected, UiOrientation orientation);

	UiListView BuildListView(
		UiDocument& document, UiNodeId parent, const UiStyle& rootStyle, const std::vector<std::string>& items, std::int32_t selected);

	UiDropdown BuildDropdown(
		UiDocument& document, UiNodeId parent, const std::vector<std::string>& options, std::int32_t selected, std::string placeholder);

	UiPopupList BuildMenu(UiDocument& document);

	UiNodeId BuildTooltip(UiDocument& document, UiNodeId target, std::string text, float delaySeconds);

	UiModal BuildModal(UiDocument& document, std::string title);

} // namespace Swim::UI::Internal
