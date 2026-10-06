#pragma once

// The engine's control types, each registered by its own widget unit (Widgets/Ui*.cpp).
// UiControlRegistry::Global() calls RegisterBuiltInControls once.
namespace Swim::UI
{

	class UiControlRegistry;

}

namespace Swim::UI::Internal
{

	void RegisterButton(UiControlRegistry& registry);
	void RegisterCheckbox(UiControlRegistry& registry);
	void RegisterToggle(UiControlRegistry& registry);
	void RegisterSlider(UiControlRegistry& registry);
	void RegisterScrollBar(UiControlRegistry& registry);
	void RegisterOption(UiControlRegistry& registry);
	void RegisterRadioGroup(UiControlRegistry& registry);
	void RegisterListView(UiControlRegistry& registry);
	void RegisterDropdown(UiControlRegistry& registry);

	inline void RegisterBuiltInControls(UiControlRegistry& registry)
	{
		RegisterButton(registry);
		RegisterCheckbox(registry);
		RegisterToggle(registry);
		RegisterSlider(registry);
		RegisterScrollBar(registry);
		RegisterOption(registry);
		RegisterRadioGroup(registry);
		RegisterListView(registry);
		RegisterDropdown(registry);
	}

} // namespace Swim::UI::Internal
