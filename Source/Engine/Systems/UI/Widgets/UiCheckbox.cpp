#include "Engine/Systems/UI/Widgets/UiCheckbox.h"

#include "Engine/Systems/UI/Internal/UiBuiltInControls.h"
#include "Engine/Systems/UI/Internal/UiWidgetHelpers.h"
#include "Engine/Systems/UI/UiControlRegistry.h"

#include <stdexcept>

namespace Swim::UI
{

	void UiCheckableControl::Flip(UiControlContext& context) const
	{
		const auto& c = context.Control();

		if (c.ReadOnly)
		{
			return;
		}

		context.ChangeCheck(c.Check == UiCheckState::Checked ? UiCheckState::Unchecked : UiCheckState::Checked);
	}

	void UiCheckableControl::OnPointerUp(UiControlContext& context, bool inside, bool)
	{
		if (inside)
		{
			Flip(context);
		}
	}

	UiActivation UiCheckableControl::OnActivate(UiControlContext& context)
	{
		Flip(context);
		return UiActivation::Handled; // A checkbox in a menu closes it like a click.
	}

	void UiCheckableControl::SetValue(UiControlContext& context, float value)
	{
		SetChecked(context, value > 0.5f ? UiCheckState::Checked : UiCheckState::Unchecked);
	}

	float UiCheckableControl::GetValue(const UiControlContext& context) const
	{
		return UiCheckValue(context.Control().Check);
	}

	void UiCheckableControl::SetChecked(UiControlContext& context, UiCheckState state)
	{
		if (static_cast<std::uint8_t>(state) > static_cast<std::uint8_t>(UiCheckState::Mixed) ||
			(!AllowsMixed() && state == UiCheckState::Mixed))
		{
			throw std::invalid_argument("Invalid check state for this control");
		}

		auto& c = context.Control();

		if (state != c.Check)
		{
			c.Check = state;
			OnCheckedFromCode(context);
			context.InvalidateArrange();
			context.InvalidateVisuals(context.GetNode());
		}
	}

	UiNodeId CreateCheckbox(UiDocument& document, UiNodeId parent, std::string label, UiCheckState state)
	{
		UiStyle row;
		row.Flow = UiFlow::Row;
		row.AlignItems = UiAlign::Center;
		const auto root = Internal::CreateStyled(document, parent, row);
		UiStyle overlay;
		overlay.Flow = UiFlow::Overlay;
		overlay.AlignItems = UiAlign::Center;
		const auto box = Internal::CreateStyled(document, root, overlay);
		const auto mark = document.Create(box);
		const auto mixed = document.Create(box);
		UiControl control;
		control.Kind = UiControlKind::Checkbox;
		control.Check = state;
		control.Parts.Track = box;
		control.Parts.Mark = mark;
		control.Parts.Mixed = mixed;

		if (!label.empty())
		{
			control.Parts.Label = Internal::ThemedLabel(document, root, std::move(label));
		}

		document.SetControl(root, control);
		document.SetThemeClass(root, UiThemeClass::Checkbox);
		document.SetThemeClass(box, UiThemeClass::CheckBox);
		document.SetThemeClass(mark, UiThemeClass::CheckMark);
		document.SetThemeClass(mixed, UiThemeClass::CheckMixed);
		return root;
	}

	void Internal::RegisterCheckbox(UiControlRegistry& registry)
	{
		registry.Register<UiCheckboxControl>("Checkbox",
			[](UiDocument& document, UiNodeId parent)
			{
				return CreateCheckbox(document, parent, "Checkbox");
			});
	}

} // namespace Swim::UI
