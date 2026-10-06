#include "Engine/Systems/UI/Widgets/UiSelection.h"

#include "Engine/Systems/UI/Internal/UiBuiltInControls.h"
#include "Engine/Systems/UI/Internal/UiWidgetHelpers.h"
#include "Engine/Systems/UI/UiControlRegistry.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Swim::UI
{

	namespace
	{

		constexpr float MaxItems = float(1u << 24);

		std::int32_t Index(float value)
		{
			return std::isfinite(value) ? static_cast<std::int32_t>(std::lround(value)) : -1;
		}

		// The owner of an option as a selection control (null when it is not one).
		UiSelectionControl* OwnerOf(const UiControlContext& option, UiNodeId& owner)
		{
			owner = option.GetPartOwner(option.GetNode());
			return owner ? dynamic_cast<UiSelectionControl*>(option.Behavior(owner)) : nullptr;
		}

	} // namespace

	void UiSelectionControl::Validate(const UiControlContext&, const UiControl& c) const
	{
		if (!std::isfinite(c.Value) || c.Value < -1.0f || c.Value != std::floor(c.Value) || c.Value >= MaxItems)
		{
			throw std::invalid_argument("Invalid UI control");
		}
	}

	float UiSelectionControl::ClampValue(const UiControlContext& context, float value) const
	{
		const auto count = static_cast<float>(context.GetOptionCount());
		value = std::isfinite(value) ? std::round(value) : -1.0f;
		return count > 0.0f ? std::clamp(value, -1.0f, count - 1.0f) : std::clamp(value, -1.0f, MaxItems - 1.0f);
	}

	std::int32_t UiSelectionControl::GetSelected(const UiControlContext& context) const
	{
		return Index(context.Control().Value);
	}

	bool UiSelectionControl::Select(UiControlContext& context, std::int32_t index, bool commit)
	{
		auto& c = context.Control();

		if (c.ReadOnly)
		{
			return false;
		}

		const auto count = static_cast<std::int32_t>(context.GetOptionCount());
		index = count == 0 ? -1 : std::clamp(index, -1, count - 1);
		const bool changed = index != Index(c.Value);

		if (changed)
		{
			c.Value = static_cast<float>(index);
			context.InvalidateVisuals(context.GetNode());
			OnValueChanged(context);
			context.Emit(UiEventKind::ValueChanged, c.Value);

			if (commit)
			{
				context.Emit(UiEventKind::ValueCommitted, c.Value);
			}
		}

		if (index >= 0)
		{
			Reveal(context, index);
		}

		return changed;
	}

	void UiSelectionControl::Reveal(UiControlContext& context, std::int32_t index) const
	{
		const auto& c = context.Control();

		if (index < 0 || !c.ScrollTarget || !context.Contains(c.ScrollTarget))
		{
			return;
		}

		const bool horizontal = IsHorizontalList(context);
		const UiRect inner = context.GetContentBox(c.ScrollTarget);
		const float viewport = horizontal ? inner.Width : inner.Height;
		float start = 0.0f;
		float length = 0.0f;

		if (c.ItemExtent > 0.0f)
		{
			start = static_cast<float>(index) * c.ItemExtent;
			length = c.ItemExtent;
		}
		else
		{
			const auto option = context.FindOption(index);

			if (!option || !context.IsLaidOut(option))
			{
				return;
			}

			const auto bounds = context.GetBounds(option);
			const auto arranged = context.GetArrangedScroll(c.ScrollTarget);
			// Bounds are laid out with the current scroll: content = bounds - inner + scroll.
			start = horizontal ? bounds.X - inner.X + arranged.X : bounds.Y - inner.Y + arranged.Y;
			length = horizontal ? bounds.Width : bounds.Height;
		}

		const auto scroll = context.GetScroll(c.ScrollTarget);
		const float current = horizontal ? scroll.X : scroll.Y;
		float wanted = current;

		if (start < wanted)
		{
			wanted = start;
		}
		else if (start + length > wanted + viewport)
		{
			wanted = start + length - viewport;
		}

		if (wanted != current)
		{
			context.SetScroll(c.ScrollTarget, horizontal, wanted);
		}
	}

	void UiSelectionControl::OnOptionPressed(UiControlContext& context, std::int32_t index)
	{
		if (context.IsAvailable())
		{
			Select(context, index, true);
		}
	}

	UiState UiSelectionControl::GetOptionState(const UiControlContext& context, std::int32_t index) const
	{
		UiState state = UiState::None;
		const auto selected = GetSelected(context);

		if (index == selected)
		{
			state = state | UiState::Checked;
		}

		if (context.IsFocused() && index == selected)
		{
			state = state | UiState::Focused;
		}

		if (context.Control().ReadOnly)
		{
			state = state | UiState::ReadOnly;
		}

		if (!context.IsAvailable())
		{
			state = state | UiState::Disabled;
		}

		return state;
	}

	UiNodeId UiOptionControl::GetFocusTarget(const UiControlContext& context) const
	{
		return context.GetPartOwner(context.GetNode()); // Options focus their owner.
	}

	void UiOptionControl::OnPointerUp(UiControlContext& context, bool inside, bool)
	{
		UiNodeId owner;

		if (auto* selection = inside ? OwnerOf(context, owner) : nullptr)
		{
			UiControlContext ownerContext(context.GetDocument(), owner);
			selection->OnOptionPressed(ownerContext, Index(context.GetPartValue(context.GetNode())));
		}
	}

	void UiOptionControl::OnPointerEnter(UiControlContext& context)
	{
		UiNodeId owner;

		if (auto* selection = OwnerOf(context, owner))
		{
			UiControlContext ownerContext(context.GetDocument(), owner);
			selection->OnOptionHovered(ownerContext, Index(context.GetPartValue(context.GetNode())));
		}
	}

	UiState UiOptionControl::GetStateFlags(const UiControlContext& context) const
	{
		UiNodeId owner;
		const auto* selection = OwnerOf(context, owner);

		if (!selection)
		{
			return UiState::None;
		}

		return selection->GetOptionState(UiControlContext(context.GetDocument(), owner), Index(context.GetPartValue(context.GetNode())));
	}

	void UiOptionControl::SetValue(UiControlContext&, float)
	{
		throw std::invalid_argument("SetValue needs a slider, scroll bar, checkbox, toggle or selection control");
	}

	UiNodeId CreateTextOption(UiDocument& document, UiNodeId parent, UiNodeId owner, std::string text)
	{
		UiStyle style;
		style.TextWrap = Text::TextWrap::None;
		const auto row = Internal::CreateStyled(document, parent, style);
		document.SetText(row, Internal::ThemeFonts(document), std::move(text), Internal::ThemeTextSize(document, UiThemeClass::MenuItem));
		document.SetPartRole(row, owner, UiPartRole::Option, float(document.GetOptionCount(owner)));
		document.SetThemeClass(row, UiThemeClass::MenuItem);
		return row;
	}

	void Internal::RegisterOption(UiControlRegistry& registry)
	{
		registry.Register<UiOptionControl>("Option");
	}

} // namespace Swim::UI
