#include "Engine/Systems/UI/Internal/UiDocumentImpl.h"
#include "Engine/Systems/UI/UiControlRegistry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

// The document side of controls: attaching behaviours, validating the shared data, routing
// value changes, part placement, post-layout and per-frame hooks, and the UiControlContext
// every behaviour talks through. Nothing here knows a particular control type; each type is a
// UiControlBehavior in Widgets/ registered by name (UiControlRegistry).
namespace Swim::UI
{

	namespace
	{

		bool Finite(float value)
		{
			return std::isfinite(value);
		}

		constexpr std::uint32_t MaxItemCount = 1u << 24;

		std::int32_t Index(float value)
		{
			return std::isfinite(value) ? static_cast<std::int32_t>(std::lround(value)) : -1;
		}

		void AddOnce(std::vector<UiNodeId>& list, UiNodeId id)
		{
			if (std::find(list.begin(), list.end(), id) == list.end())
			{
				list.push_back(id);
			}
		}

	} // namespace

	// --- UiControlBehavior defaults -----------------------------------------------------

	float UiControlBehavior::ClampValue(const UiControlContext& context, float value) const
	{
		const auto& c = context.Control();

		if (!Finite(value))
		{
			value = c.Min;
		}

		value = std::clamp(value, c.Min, c.Max);

		if (c.Step > 0.0f)
		{
			value = c.Min + std::round((value - c.Min) / c.Step) * c.Step;
			value = std::clamp(value, c.Min, c.Max);
		}

		return value;
	}

	void UiControlBehavior::SetValue(UiControlContext& context, float value)
	{
		value = ClampValue(context, value);
		auto& c = context.Control();

		if (value != c.Value)
		{
			c.Value = value;
			OnValueChanged(context);
			context.InvalidateArrange();
			context.InvalidateVisuals(context.GetNode());
		}
	}

	float UiControlBehavior::GetValue(const UiControlContext& context) const
	{
		return context.Control().Value;
	}

	void UiControlBehavior::SetChecked(UiControlContext&, UiCheckState)
	{
		throw std::invalid_argument("SetChecked needs a checkbox or toggle");
	}

	UiState UiControlBehavior::GetStateFlags(const UiControlContext& context) const
	{
		const auto& c = context.Control();
		UiState state = UiState::None;

		if (c.Check == UiCheckState::Checked)
		{
			state = state | UiState::Checked;
		}
		else if (c.Check == UiCheckState::Mixed)
		{
			state = state | UiState::Mixed;
		}

		if (c.ReadOnly)
		{
			state = state | UiState::ReadOnly;
		}

		return state;
	}

	// --- UiControlContext ----------------------------------------------------------------

	UiControl& UiControlContext::Control() const
	{
		return document.impl->Get(node).Control;
	}

	UiControlBehavior* UiControlContext::Behavior(UiNodeId other) const
	{
		const auto* found = document.impl->Find(other);
		return found ? found->Behavior.get() : nullptr;
	}

	bool UiControlContext::IsHovered() const
	{
		return document.impl->Hover == node;
	}

	bool UiControlContext::IsPressed() const
	{
		return document.impl->Pressed == node;
	}

	bool UiControlContext::IsFocused() const
	{
		return document.impl->Focused == node;
	}

	bool UiControlContext::IsDragging() const
	{
		return document.impl->Dragging == node;
	}

	bool UiControlContext::IsAvailable(UiNodeId other) const
	{
		return document.impl->Available(other ? other : node);
	}

	bool UiControlContext::InputAllowed(UiNodeId other) const
	{
		return document.impl->InputAllowed(other);
	}

	UiNodeId UiControlContext::GetFocus() const
	{
		return document.impl->Focused;
	}

	bool UiControlContext::Contains(UiNodeId other) const
	{
		return other && document.impl->Nodes.contains(other.Value);
	}

	bool UiControlContext::IsLaidOut(UiNodeId other) const
	{
		const auto* found = document.impl->Find(other);
		return found && found->Active;
	}

	UiNodeId UiControlContext::GetParent(UiNodeId other) const
	{
		const auto* found = document.impl->Find(other);
		return found ? found->Parent : UiNodeId{};
	}

	const std::vector<UiNodeId>& UiControlContext::GetChildren(UiNodeId other) const
	{
		return document.impl->Get(other).Children;
	}

	bool UiControlContext::IsInside(UiNodeId other, UiNodeId ancestor) const
	{
		const auto& impl = *document.impl;

		if (!impl.Nodes.contains(other.Value))
		{
			return false;
		}

		for (auto current = impl.Get(other).Parent; current; current = impl.Get(current).Parent)
		{
			if (current == ancestor)
			{
				return true;
			}
		}

		return false;
	}

	const UiStyle& UiControlContext::GetStyle(UiNodeId other) const
	{
		return document.impl->Get(other).Style;
	}

	UiRect UiControlContext::GetBounds(UiNodeId other) const
	{
		return document.impl->Get(other).Bounds;
	}

	UiRect UiControlContext::GetContentBox(UiNodeId other) const
	{
		const auto& n = document.impl->Get(other);
		return Internal::ContentBox(n.Bounds, n.Style.Padding);
	}

	UiPoint UiControlContext::GetDesiredSize(UiNodeId other) const
	{
		const auto* found = document.impl->Find(other);
		return found ? found->Desired : UiPoint{};
	}

	UiPoint UiControlContext::GetScroll(UiNodeId other) const
	{
		return document.impl->Get(other).Scroll;
	}

	UiPoint UiControlContext::GetMaxScroll(UiNodeId other) const
	{
		return document.impl->Get(other).MaxScroll;
	}

	UiPoint UiControlContext::GetArrangedScroll(UiNodeId other) const
	{
		return document.impl->Get(other).ArrangedScroll;
	}

	void UiControlContext::SetScroll(UiNodeId other, bool horizontal, float offset) const
	{
		auto& scroll = document.impl->Get(other).Scroll;
		float& axis = horizontal ? scroll.X : scroll.Y;
		const float value = Finite(offset) ? std::max(0.0f, offset) : 0.0f;

		if (axis != value)
		{
			axis = value; // Clamped to the content by the next Layout.
			document.impl->Dirty = true;
		}
	}

	bool UiControlContext::ChangeValue(float value, bool commit) const
	{
		return document.impl->ChangeValue(document.impl->Get(node), value, commit);
	}

	void UiControlContext::Commit(float previous) const
	{
		const float value = Control().Value;

		if (value != previous)
		{
			document.impl->QueueEvent({ UiEventKind::ValueCommitted, node, value });
		}
	}

	void UiControlContext::ChangeCheck(UiCheckState state) const
	{
		auto& impl = *document.impl;
		auto& n = impl.Get(node);
		n.Control.Check = state;
		impl.MarkControlDirty(n);
		const float value = UiCheckValue(state);
		impl.QueueEvent({ UiEventKind::ValueChanged, node, value });
		impl.QueueEvent({ UiEventKind::ValueCommitted, node, value });
	}

	void UiControlContext::Emit(UiEventKind kind, float value) const
	{
		document.impl->QueueEvent({ kind, node, value });
	}

	void UiControlContext::Emit(UiEventKind kind, UiNodeId target, float value) const
	{
		document.impl->QueueEvent({ kind, target, value });
	}

	void UiControlContext::InvalidateArrange() const
	{
		document.impl->Dirty = true;
	}

	void UiControlContext::InvalidateLayout(UiNodeId other) const
	{
		document.impl->MarkLayoutDirty(other);
	}

	void UiControlContext::InvalidateVisuals(UiNodeId subtree) const
	{
		document.impl->MarkSubtreeVisualDirty(subtree);
	}

	void UiControlContext::InvalidatePaint(UiNodeId other) const
	{
		if (auto* found = document.impl->Find(other))
		{
			found->PaintDirty = true;
		}
	}

	void UiControlContext::PlacePartNow(UiNodeId part) const
	{
		document.impl->PlacePartNow(part);
	}

	void UiControlContext::SetHidden(bool hidden) const
	{
		auto& n = document.impl->Get(node);

		if (n.ControlHidden != hidden)
		{
			n.ControlHidden = hidden;
			document.impl->MarkSubtreeVisualDirty(node);
		}
	}

	bool UiControlContext::IsHidden() const
	{
		return document.impl->Get(node).ControlHidden;
	}

	void UiControlContext::SetOpacity(float opacity) const
	{
		auto& n = document.impl->Get(node);

		if (n.ControlOpacity != opacity)
		{
			n.ControlOpacity = opacity;
			n.PaintDirty = true;
		}
	}

	const std::string& UiControlContext::GetText(UiNodeId other) const
	{
		return document.impl->Get(other).TextContents;
	}

	bool UiControlContext::HasFonts(UiNodeId other) const
	{
		const auto* found = document.impl->Find(other);
		return found && found->Fonts;
	}

	bool UiControlContext::IsEditable(UiNodeId other) const
	{
		const auto* found = document.impl->Find(other);
		return found && found->Editable;
	}

	void UiControlContext::SetText(UiNodeId other, std::string text) const
	{
		auto& impl = *document.impl;
		auto& label = impl.Get(other);

		if (label.TextContents == text)
		{
			return;
		}

		label.TextContents = std::move(text);
		label.TextLayout.reset();
		label.MeasureLayout.reset();
		label.Selection = impl.ClampSelection(label, label.Selection);
		impl.MarkLayoutDirty(other);
	}

	std::uint32_t UiControlContext::GetOptionCount() const
	{
		return document.impl->OptionCount(document.impl->Get(node));
	}

	UiNodeId UiControlContext::FindOption(std::int32_t index) const
	{
		return document.impl->OptionFor(document.impl->Get(node), index);
	}

	UiNodeId UiControlContext::GetPartOwner(UiNodeId part) const
	{
		const auto* found = document.impl->Find(part);
		return found && found->PartOf && document.impl->Nodes.contains(found->PartOf.Value) ? found->PartOf : UiNodeId{};
	}

	float UiControlContext::GetPartValue(UiNodeId part) const
	{
		return document.impl->Get(part).PartValue;
	}

	void UiControlContext::OpenPopup(UiNodeId popup, const UiPopupDesc& desc) const
	{
		document.impl->OpenPopupEntry(popup, desc, true);
	}

	bool UiControlContext::ClosePopup(UiNodeId popup) const
	{
		return document.ClosePopup(popup);
	}

	bool UiControlContext::IsPopupOpen(UiNodeId popup) const
	{
		return popup && document.IsPopupOpen(popup);
	}

	// --- Document plumbing ------------------------------------------------------------------

	void UiDocument::Impl::MarkControlDirty(Node& control)
	{
		Dirty = true; // Part geometry is arrangement only.
		MarkSubtreeVisualDirty(control.Id);
	}

	bool UiDocument::Impl::ChangeValue(Node& node, float value, bool commit)
	{
		auto context = Context(node.Id);
		value = node.Behavior ? node.Behavior->ClampValue(context, value) : value;
		auto& c = node.Control;
		const bool changed = value != c.Value;

		if (changed)
		{
			c.Value = value;

			if (node.Behavior)
			{
				node.Behavior->OnValueChanged(context);
			}

			MarkControlDirty(node);
			QueueEvent({ UiEventKind::ValueChanged, node.Id, value });
		}

		if (commit && changed)
		{
			QueueEvent({ UiEventKind::ValueCommitted, node.Id, value });
		}

		return changed;
	}

	void UiDocument::Impl::ValidateControl(const Node& node, const UiControlBehavior& behavior, const UiControl& c) const
	{
		const bool valid = node.Role != UiPartRole::Option && Finite(c.ItemExtent) && c.ItemExtent >= 0.0f &&
			c.ItemExtent <= Internal::MaxLogical && c.ItemCount <= MaxItemCount &&
			static_cast<std::uint8_t>(c.Orientation) <= static_cast<std::uint8_t>(UiOrientation::Vertical) &&
			static_cast<std::uint8_t>(c.Check) <= static_cast<std::uint8_t>(UiCheckState::Mixed) &&
			static_cast<std::uint8_t>(c.TrackClick) <= static_cast<std::uint8_t>(UiTrackClick::Page) &&
			static_cast<std::uint8_t>(c.Visibility) <= static_cast<std::uint8_t>(UiScrollBarVisibility::Overlay) && Finite(c.Min) &&
			Finite(c.Max) && c.Min <= c.Max && std::abs(c.Min) <= 1e9f && std::abs(c.Max) <= 1e9f && Finite(c.Step) && c.Step >= 0.0f &&
			Finite(c.PageStep) && c.PageStep >= 0.0f && Finite(c.MinThumbLength) && c.MinThumbLength >= 0.0f &&
			c.MinThumbLength <= Internal::MaxLogical && Finite(c.FadeDelaySeconds) && c.FadeDelaySeconds >= 0.0f &&
			c.FadeDelaySeconds <= 60.0f && Finite(c.FadeSeconds) && c.FadeSeconds >= 0.0f && c.FadeSeconds <= 60.0f &&
			c.LabelDecimals >= -1 && c.LabelDecimals <= 9;

		if (!valid)
		{
			throw std::invalid_argument("Invalid UI control");
		}

		const auto isDescendant = [&](UiNodeId id, UiNodeId ancestor)
		{
			for (auto current = Get(id).Parent; current; current = Get(current).Parent)
			{
				if (current == ancestor)
				{
					return true;
				}
			}

			return false;
		};
		const auto& parts = c.Parts;

		for (const auto part :
			{ parts.Track, parts.Fill, parts.Thumb, parts.Mark, parts.Mixed, parts.Label, parts.Decrement, parts.Increment })
		{
			// Some controls only display through a part placed anywhere (a slider's value label).
			const bool anywhere = part == parts.Label && behavior.LabelMayBeOutside();

			if (part && (!Nodes.contains(part.Value) || (!anywhere && !isDescendant(part, node.Id)) || part == node.Id))
			{
				throw std::invalid_argument("UI control parts must be descendants of the control");
			}
		}

		if (c.ScrollTarget && (!behavior.UsesScrollTarget() || !Nodes.contains(c.ScrollTarget.Value) || c.ScrollTarget == node.Id))
		{
			throw std::invalid_argument("Only scroll bars, list views and dropdowns have a scroll target");
		}

		if (parts.Popup && (!behavior.UsesPopup() || !Nodes.contains(parts.Popup.Value) || Get(parts.Popup).Parent != Root))
		{
			throw std::invalid_argument("A dropdown's popup must be a child of the UI root");
		}

		behavior.Validate(Context(node.Id), c);
	}

	void UiDocument::Impl::DetachControl(Node& node)
	{
		const auto& old = node.Control.Parts;

		for (const auto part : { old.Track, old.Fill, old.Thumb, old.Mark, old.Mixed, old.Label, old.Decrement, old.Increment })
		{
			if (part && Nodes.contains(part.Value) && Get(part).PartOf == node.Id)
			{
				auto& p = Get(part);
				p.PartOf = {};
				p.Role = UiPartRole::None;
				MarkLayoutDirty(part);
			}
		}

		if (Dragging == node.Id)
		{
			Dragging = {};
		}
	}

	void UiDocument::Impl::ApplyControl(UiNodeId id, std::unique_ptr<UiControlBehavior> behavior, const UiControl& control)
	{
		auto& node = Get(id);
		UiControlBehavior* incoming = behavior ? behavior.get() : node.Behavior.get();

		if (control.Kind != UiControlKind::None && incoming)
		{
			ValidateControl(node, *incoming, control); // Throws before anything changes.
		}
		else if (control.Kind != UiControlKind::None)
		{
			throw std::invalid_argument("A custom UI control needs a behaviour (SetControl with a type name or an instance)");
		}

		const bool wasOwner = node.Behavior && node.Behavior->OwnsOptions();
		DetachControl(node);

		if (control.Kind == UiControlKind::None)
		{
			node.Behavior.reset();
		}
		else if (behavior)
		{
			node.Behavior = std::move(behavior);
		}

		node.Control = control;
		node.ControlHidden = false;
		node.ControlOpacity = 1.0f;
		const bool owner = node.Behavior && node.Behavior->OwnsOptions();

		if (owner)
		{
			const auto count = OptionCount(node);
			node.Control.Value = count > 0 ? std::min(control.Value, float(count) - 1.0f) : control.Value;
		}
		else
		{
			if (node.Behavior)
			{
				node.Control.Value = node.Behavior->ClampValue(Context(id), control.Value);
			}

			if (wasOwner)
			{
				node.Options.clear(); // The options stay registered to nothing; they no longer select.
			}
		}

		const std::pair<UiNodeId, UiPartRole> roles[] = { { control.Parts.Track, UiPartRole::Track },
			{ control.Parts.Fill, UiPartRole::Fill }, { control.Parts.Thumb, UiPartRole::Thumb }, { control.Parts.Mark, UiPartRole::Mark },
			{ control.Parts.Mixed, UiPartRole::Mixed }, { control.Parts.Label, UiPartRole::Label },
			{ control.Parts.Decrement, UiPartRole::Decrement }, { control.Parts.Increment, UiPartRole::Increment } };

		if (node.Behavior)
		{
			for (const auto& [part, role] : roles)
			{
				if (part)
				{
					auto& p = Get(part);
					p.PartOf = id;
					p.Role = role;
					MarkLayoutDirty(part);
				}
			}

			// A themed control's axes follow its orientation.
			for (const auto& [part, role] : roles)
			{
				if (part && Get(part).ThemeClass != UiThemeClass::None)
				{
					ApplyTheme(Get(part));
				}
			}
		}

		if (node.ThemeClass != UiThemeClass::None)
		{
			ApplyTheme(node);
		}

		if (node.Behavior)
		{
			auto context = Context(id);
			node.Behavior->OnAttached(context);

			if (node.Behavior->WantsUpdates())
			{
				AddOnce(UpdatedControls, id);
			}

			if (node.Behavior->WantsArranged())
			{
				AddOnce(ArrangedControls, id);
			}
		}

		MarkLayoutDirty(id);
		MarkSubtreeVisualDirty(id);
		ClearUnavailable();
	}

	std::optional<UiRect> UiDocument::Impl::PartGeometry(const Node& part, const UiRect& inner) const
	{
		const auto* owner = part.PartOf ? Find(part.PartOf) : nullptr;

		if (!owner || !owner->Behavior)
		{
			return std::nullopt;
		}

		return owner->Behavior->PlacePart(Context(owner->Id), part.Id, part.Role, part.PartValue, inner);
	}

	void UiDocument::Impl::ReArrange(Node& node, UiRect bounds)
	{
		const auto begin = std::find(Order.begin(), Order.end(), node.Id);

		if (begin == Order.end() || !node.Parent)
		{
			return;
		}

		const auto isInside = [&](UiNodeId id)
		{
			for (auto current = Get(id).Parent; current; current = Get(current).Parent)
			{
				if (current == node.Id)
				{
					return true;
				}
			}

			return false;
		};
		auto end = std::next(begin);

		while (end != Order.end() && isInside(*end))
		{
			++end;
		}

		// [prefix][subtree][tail] -> [prefix][tail] -> Arrange appends the new subtree ->
		// [prefix][new subtree][tail], in place (no temporary copy of the tail).
		const auto prefix = static_cast<std::size_t>(std::distance(Order.begin(), begin));
		const auto subtree = static_cast<std::size_t>(std::distance(begin, end));
		const auto tail = Order.size() - prefix - subtree;
		std::rotate(begin, end, Order.end());
		Order.resize(prefix + tail);
		const auto& parent = Get(node.Parent);
		const UiRect parentInner = Internal::ContentBox(parent.Bounds, parent.Style.Padding);
		const UiRect clip = parent.Style.Clip ? Internal::Intersect(parent.Clip, parentInner) : parent.Clip;
		Arrange(node, bounds, clip, parent.Active);
		const auto tailBegin = Order.begin() + static_cast<std::ptrdiff_t>(prefix);
		std::rotate(tailBegin, tailBegin + static_cast<std::ptrdiff_t>(tail), Order.end());
	}

	void UiDocument::Impl::PlacePartNow(UiNodeId partId)
	{
		auto* part = Find(partId);

		if (!part || !part->Style.Visible || !part->Parent)
		{
			return;
		}

		const auto& parent = Get(part->Parent);
		const UiRect inner = Internal::ContentBox(parent.Bounds, parent.Style.Padding);

		if (const auto rect = PartGeometry(*part, inner))
		{
			const UiRect placed{ rect->X + inner.X - parent.Scroll.X, rect->Y + inner.Y - parent.Scroll.Y, rect->Width, rect->Height };

			if (!Internal::SameRect(placed, part->Bounds))
			{
				ReArrange(*part, placed);
			}
		}
	}

	void UiDocument::Impl::NotifyArranged()
	{
		// Index loop: a hook may attach or detach controls.
		std::size_t write = 0;

		for (std::size_t read = 0; read < ArrangedControls.size(); ++read)
		{
			const auto id = ArrangedControls[read];
			auto* node = Find(id);

			if (!node || !node->Behavior || !node->Behavior->WantsArranged())
			{
				continue; // Dropped from the list.
			}

			ArrangedControls[write++] = id;
			auto context = Context(id);
			node->Behavior->OnArranged(context);
		}

		ArrangedControls.resize(write);
	}

	void UiDocument::Impl::CancelCapture()
	{
		if (!Dragging)
		{
			return;
		}

		const auto id = Dragging;
		Dragging = {};

		if (auto* node = Find(id); node && node->Behavior)
		{
			MarkSubtreeVisualDirty(id);
			auto context = Context(id);
			node->Behavior->OnPointerCancel(context, true); // Keeps (and commits) the value reached so far.
		}
	}

	bool UiDocument::Impl::AnimateControls(float seconds)
	{
		bool animating = false;
		std::size_t write = 0;

		for (std::size_t read = 0; read < UpdatedControls.size(); ++read)
		{
			const auto id = UpdatedControls[read];
			auto* node = Find(id);

			if (!node || !node->Behavior || !node->Behavior->WantsUpdates())
			{
				continue;
			}

			UpdatedControls[write++] = id;
			auto context = Context(id);
			animating = node->Behavior->OnUpdate(context, seconds) || animating;
		}

		UpdatedControls.resize(write);
		return animating;
	}

	std::uint32_t UiDocument::Impl::OptionCount(const Node& owner) const
	{
		if (owner.Control.ItemCount > 0)
		{
			return owner.Control.ItemCount;
		}

		std::uint32_t count = 0;

		for (const auto option : owner.Options)
		{
			const auto* node = Find(option);

			if (node && node->PartOf == owner.Id && node->Role == UiPartRole::Option)
			{
				++count;
			}
		}

		return count;
	}

	UiNodeId UiDocument::Impl::OptionFor(const Node& owner, std::int32_t index) const
	{
		if (index < 0)
		{
			return {};
		}

		for (const auto option : owner.Options)
		{
			const auto* node = Find(option);

			if (node && node->PartOf == owner.Id && node->Role == UiPartRole::Option && Index(node->PartValue) == index)
			{
				return option;
			}
		}

		return {};
	}

	// --- Public API ---------------------------------------------------------------------------

	void UiDocument::SetControl(UiNodeId id, const UiControl& control)
	{
		auto& node = impl->Get(id);

		if (control.Kind == UiControlKind::Option || static_cast<std::uint8_t>(control.Kind) > static_cast<std::uint8_t>(UiControlKind::Custom))
		{
			throw std::invalid_argument("Invalid UI control kind (options are registered with SetPartRole)");
		}

		std::unique_ptr<UiControlBehavior> behavior;

		if (control.Kind != UiControlKind::None && control.Kind != UiControlKind::Custom)
		{
			const auto name = UiControlTypeName(control.Kind);

			// Same built-in type: keep the instance (its derived state), replace the data.
			if (!node.Behavior || node.Behavior->GetTypeName() != name)
			{
				behavior = UiControlRegistry::Global().Create(name);

				if (!behavior)
				{
					throw std::invalid_argument("UI control type is not registered: " + std::string(name));
				}
			}
		}

		impl->ApplyControl(id, std::move(behavior), control);
	}

	void UiDocument::SetControl(UiNodeId id, std::string_view type, const UiControl& control)
	{
		impl->Get(id); // Unknown nodes throw first.
		auto behavior = UiControlRegistry::Global().Create(type);

		if (!behavior)
		{
			throw std::invalid_argument("Unknown UI control type: " + std::string(type));
		}

		if (type == UiControlTypeName(UiControlKind::Option))
		{
			throw std::invalid_argument("Options are registered with SetPartRole");
		}

		UiControl data = control;
		data.Kind = UiControlKind::Custom;

		for (std::uint8_t kind = 1; kind < static_cast<std::uint8_t>(UiControlKind::Option); ++kind)
		{
			if (UiControlTypeName(static_cast<UiControlKind>(kind)) == type)
			{
				data.Kind = static_cast<UiControlKind>(kind);
			}
		}

		impl->ApplyControl(id, std::move(behavior), data);
	}

	void UiDocument::SetControl(UiNodeId id, std::unique_ptr<UiControlBehavior> behavior, const UiControl& control)
	{
		impl->Get(id);

		if (!behavior)
		{
			throw std::invalid_argument("SetControl needs a behaviour instance");
		}

		UiControl data = control;
		data.Kind = UiControlKind::Custom;
		impl->ApplyControl(id, std::move(behavior), data);
	}

	UiControlBehavior* UiDocument::GetControlBehavior(UiNodeId id) const
	{
		return impl->Get(id).Behavior.get();
	}

	const UiControl& UiDocument::GetControl(UiNodeId id) const
	{
		return impl->Get(id).Control;
	}

	void UiDocument::SetValue(UiNodeId id, float value)
	{
		auto& node = impl->Get(id);

		if (!node.Behavior)
		{
			throw std::invalid_argument("SetValue needs a control");
		}

		auto context = impl->Context(id);
		node.Behavior->SetValue(context, value);
	}

	float UiDocument::GetValue(UiNodeId id) const
	{
		const auto& node = impl->Get(id);
		return node.Behavior ? node.Behavior->GetValue(impl->Context(id)) : node.Control.Value;
	}

	void UiDocument::SetChecked(UiNodeId id, UiCheckState state)
	{
		auto& node = impl->Get(id);

		if (!node.Behavior)
		{
			throw std::invalid_argument("SetChecked needs a checkbox or toggle");
		}

		auto context = impl->Context(id);
		node.Behavior->SetChecked(context, state);
	}

	UiCheckState UiDocument::GetChecked(UiNodeId id) const
	{
		return impl->Get(id).Control.Check;
	}

	void UiDocument::SetPartRole(UiNodeId partId, UiNodeId controlId, UiPartRole role, float value)
	{
		auto& part = impl->Get(partId);
		const auto release = [&]
		{
			if (part.Role == UiPartRole::Option)
			{
				if (part.PartOf && impl->Nodes.contains(part.PartOf.Value))
				{
					std::erase(impl->Get(part.PartOf).Options, partId);
				}

				part.Control.Kind = UiControlKind::None;
				part.Behavior.reset();
				impl->ClearUnavailable();
			}

			part.PartOf = {};
			part.Role = UiPartRole::None;
			impl->MarkLayoutDirty(partId);
			impl->MarkSubtreeVisualDirty(partId);
		};

		if (role == UiPartRole::None)
		{
			release();
			return;
		}

		auto& control = impl->Get(controlId);

		if (role == UiPartRole::Option)
		{
			const auto isDescendant = [&]
			{
				for (auto current = part.Parent; current; current = impl->Get(current).Parent)
				{
					if (current == controlId)
					{
						return true;
					}
				}

				return false;
			}();
			const bool owner = control.Behavior && control.Behavior->OwnsOptions();
			const bool anywhere = owner && control.Behavior->OptionsMayBeOutside();
			// Options live inside their owner, except where the owner says otherwise (a
			// dropdown's options are in its popup).
			if (!owner || !std::isfinite(value) || value < 0.0f || value >= float(MaxItemCount) || value != std::floor(value) ||
				partId == controlId || (!anywhere && !isDescendant) || (part.Behavior && part.Role != UiPartRole::Option))
			{
				throw std::invalid_argument("Options are nodes without another control, inside a radio group or list view (or anywhere "
											"for a dropdown), at integral indices");
			}

			if (part.Role == UiPartRole::Option && part.PartOf == controlId && part.PartValue == value)
			{
				return;
			}

			if (part.Role != UiPartRole::None)
			{
				release();
			}

			part.PartOf = controlId;
			part.Role = role;
			part.PartValue = value;
			part.Control = {};
			part.Control.Kind = UiControlKind::Option;
			part.Behavior = UiControlRegistry::Global().Create(UiControlTypeName(UiControlKind::Option));
			control.Options.push_back(partId);
			impl->MarkLayoutDirty(partId);
			impl->MarkSubtreeVisualDirty(partId);

			if (Index(control.Control.Value) == Index(value))
			{
				// The owner shows its selection again (a dropdown's label).
				auto context = impl->Context(controlId);
				control.Behavior->OnValueChanged(context);
			}

			return;
		}

		if (role != UiPartRole::Tick || !control.Behavior || !control.Behavior->AcceptsTicks() || part.Parent != controlId ||
			!std::isfinite(value))
		{
			throw std::invalid_argument("SetPartRole registers slider tick marks (direct children) at finite values, and options");
		}

		if (part.Role == UiPartRole::Option)
		{
			release();
		}

		part.PartOf = controlId;
		part.Role = role;
		part.PartValue = value;

		if (part.ThemeClass != UiThemeClass::None)
		{
			impl->ApplyTheme(part);
		}

		impl->MarkLayoutDirty(partId);
	}

	UiNodeId UiDocument::FindOption(UiNodeId owner, std::uint32_t index) const
	{
		const auto& node = impl->Get(owner);
		return index > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())
			? UiNodeId{}
			: impl->OptionFor(node, static_cast<std::int32_t>(index));
	}

	std::uint32_t UiDocument::GetOptionCount(UiNodeId owner) const
	{
		return impl->OptionCount(impl->Get(owner));
	}

} // namespace Swim::UI
