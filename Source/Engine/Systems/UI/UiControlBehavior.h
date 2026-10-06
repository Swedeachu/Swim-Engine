#pragma once

#include "Engine/Systems/UI/UiDocument.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Control behaviours: what a control does with input, written as one small class per control
// type and registered by name (UiControlRegistry). The document owns everything generic -
// hit testing, hover, press, focus, pointer capture, keyboard routing, popups, events and
// callbacks - and calls the node's behaviour only for the interactions it cares about. A
// control never tracks which node is hovered, pressed or focused; it asks its context.
//
//   class Knob final : public UiControlBehavior
//   {
//     public:
//       UiPointerResponse OnPointerDown(UiControlContext& c, const UiPointerInput& p) override
//       {
//           grab = p.Position.Y;
//           start = c.Control().Value;
//           return UiPointerResponse::Capture;      // drags reach OnPointerDrag until release
//       }
//
//       void OnPointerDrag(UiControlContext& c, const UiPointerInput& p) override
//       {
//           c.ChangeValue(start + (grab - p.Position.Y) * 0.01f, false); // clamped, ValueChanged
//       }
//
//       void OnPointerUp(UiControlContext& c, bool, bool) override { c.Commit(start); }
//
//     private:
//       float grab = 0.0f, start = 0.0f;
//   };
//   SWIM_UI_CONTROL(Knob, "Game.Knob");                  // or document.AttachControl<Knob>(node)
//
//   document.SetControl(node, "Game.Knob", UiControl{ .Min = 0, .Max = 1 });
//   document.OnValue(node, [&](float v) { volume = v; });
namespace Swim::UI
{

	// A pointer as a control sees it: logical canvas units, already mapped from framebuffer
	// pixels (and from world space for world canvases).
	struct UiPointerInput
	{
		UiPoint Position;
		UiKeyModifiers Modifiers;
	};

	enum class UiPointerResponse : std::uint8_t
	{
		Ignore,	 // Nothing to do (the press still focuses and clicks normally).
		Handled, // The control reacted to the press.
		Capture, // As Handled, and every pointer move goes to OnPointerDrag until release or cancel.
	};

	// What Enter/Space/gamepad A on a focused control did.
	enum class UiActivation : std::uint8_t
	{
		Click,	  // Not handled: the document emits Click (and closes an activating menu).
		Handled,  // The control changed (checkbox toggled): the document closes an activating menu.
		KeepOpen, // The control handled it and owns any popup state (dropdowns).
	};

	class UiControlBehavior;

	// A control's window into the document, for one node, during one call. Cheap to create;
	// never stored. Everything takes logical canvas units.
	class UiControlContext
	{

	  public:

		UiControlContext(UiDocument& document, UiNodeId node) : document(document), node(node) {}

		UiNodeId GetNode() const { return node; }

		UiDocument& GetDocument() const { return document; }

		// The node's shared control data (value, range, check state, parts, ...).
		UiControl& Control() const;

		UiControlBehavior* Behavior(UiNodeId other) const;

		// --- Interaction state the document tracks ---
		bool IsHovered() const;

		bool IsPressed() const;

		bool IsFocused() const;

		bool IsDragging() const; // This control holds the pointer capture.

		bool IsAvailable(UiNodeId other = {}) const; // Visible and enabled with every ancestor.

		bool InputAllowed(UiNodeId other) const;		 // Not below an open modal.

		UiNodeId GetFocus() const;

		// --- Tree and geometry (of the last Layout) ---
		bool Contains(UiNodeId other) const;

		bool IsLaidOut(UiNodeId other) const; // Arranged this layout (visible).

		UiNodeId GetParent(UiNodeId other) const;

		const std::vector<UiNodeId>& GetChildren(UiNodeId other) const;

		bool IsInside(UiNodeId other, UiNodeId ancestor) const; // A strict descendant.

		const UiStyle& GetStyle(UiNodeId other) const;

		UiRect GetBounds(UiNodeId other) const;

		UiRect GetContentBox(UiNodeId other) const; // Bounds minus padding.

		UiPoint GetDesiredSize(UiNodeId other) const;

		UiPoint GetScroll(UiNodeId other) const;

		UiPoint GetMaxScroll(UiNodeId other) const;

		UiPoint GetArrangedScroll(UiNodeId other) const; // The offset its children were arranged with.

		// Sets one scroll axis of a node (kept >= 0, clamped to its content at the next Layout).
		void SetScroll(UiNodeId other, bool horizontal, float offset) const;

		// --- Value changes from input ---
		// Clamps through the behaviour, stores, runs OnValueChanged and emits ValueChanged
		// (and ValueCommitted when commit). True when the value changed.
		bool ChangeValue(float value, bool commit) const;

		// Emits ValueCommitted for the current value when it differs from `previous` (the end
		// of a drag that changed it).
		void Commit(float previous) const;

		// Sets the check state from input: ValueChanged + ValueCommitted with 0, 1 or 0.5.
		void ChangeCheck(UiCheckState state) const;

		void Emit(UiEventKind kind, float value = 0.0f) const;

		void Emit(UiEventKind kind, UiNodeId target, float value) const;

		// --- Invalidation ---
		void InvalidateArrange() const;				 // Part placement changed (no remeasure).

		void InvalidateLayout(UiNodeId other) const; // A node's measurement changed (text, size).

		void InvalidateVisuals(UiNodeId subtree) const; // State-dependent paint of a subtree.

		void InvalidatePaint(UiNodeId other) const;

		// Re-places a part this control positions (PlacePart) right away, after Layout.
		void PlacePartNow(UiNodeId part) const;

		// Not painted and not hit (auto scroll bars without overflow); parts follow.
		void SetHidden(bool hidden) const;

		bool IsHidden() const;

		void SetOpacity(float opacity) const; // Multiplies the control's paint (fades).

		// --- Text of parts ---
		const std::string& GetText(UiNodeId other) const;

		bool HasFonts(UiNodeId other) const;

		bool IsEditable(UiNodeId other) const;

		// Replaces a node's text content, keeping its fonts and size.
		void SetText(UiNodeId other, std::string text) const;

		// --- Options (selection owners; SetPartRole registers them) ---
		std::uint32_t GetOptionCount() const;		   // UiControl::ItemCount, else the registered options.

		UiNodeId FindOption(std::int32_t index) const; // Empty when not bound.

		UiNodeId GetPartOwner(UiNodeId part) const;	   // The control a part (or option) belongs to.

		float GetPartValue(UiNodeId part) const;	   // A tick's value, an option's index.

		// --- Popups ---
		// Opens a popup this control owns (dropdown lists); OnPopupLaidOut runs once it is laid out.
		void OpenPopup(UiNodeId popup, const UiPopupDesc& desc) const;

		bool ClosePopup(UiNodeId popup) const;

		bool IsPopupOpen(UiNodeId popup) const;

	  private:

		UiDocument& document;
		UiNodeId node;

	};

	// The behaviour of one control node. Every hook has a default, so a control overrides
	// only what it reacts to. One instance per node (state such as a drag offset or an
	// animation lives in the instance).
	class UiControlBehavior
	{

	  public:

		virtual ~UiControlBehavior() = default;

		// The registered name ("Slider", "Game.Knob"; empty for unregistered instances).
		const std::string& GetTypeName() const { return typeName; }

		// --- Traits ---
		virtual bool IsFocusable() const { return true; }

		virtual bool OwnsOptions() const { return false; }		  // Options attach with SetPartRole(.., Option, index).

		virtual bool OptionsMayBeOutside() const { return false; } // Options anywhere (a dropdown's popup).

		virtual bool AcceptsTicks() const { return false; }		  // SetPartRole(.., Tick, value) on direct children.

		virtual bool UsesScrollTarget() const { return false; }

		virtual bool LabelMayBeOutside() const { return false; } // Parts.Label anywhere (a slider's value display).

		virtual bool UsesPopup() const { return false; }

		virtual bool WantsUpdates() const { return false; }	// OnUpdate on every UiDocument::Update.

		virtual bool WantsArranged() const { return false; } // OnArranged after every Layout.

		// --- Lifecycle ---
		// Throws std::invalid_argument when `control` does not suit this type (checked before
		// anything changes; the document validates ranges and that parts are descendants).
		virtual void Validate(const UiControlContext& context, const UiControl& control) const {}

		// The node's data was (re)applied (SetControl): initialize derived state.
		virtual void OnAttached(UiControlContext& context) {}

		// --- Values ---
		virtual float ClampValue(const UiControlContext& context, float value) const;

		// The value changed (input or SetValue): sync parts and targets.
		virtual void OnValueChanged(UiControlContext& context) {}

		// From code: no events. Default: clamp and store.
		virtual void SetValue(UiControlContext& context, float value);

		virtual float GetValue(const UiControlContext& context) const;

		// From code (checkboxes, toggles); others throw std::invalid_argument.
		virtual void SetChecked(UiControlContext& context, UiCheckState state);

		// --- Input ---
		virtual UiPointerResponse OnPointerDown(UiControlContext& context, const UiPointerInput& pointer) { return UiPointerResponse::Ignore; }

		virtual void OnPointerDrag(UiControlContext& context, const UiPointerInput& pointer) {}

		// The press ended (inside: released over this control). captured: it held the capture.
		virtual void OnPointerUp(UiControlContext& context, bool inside, bool captured) {}

		// The capture or press was lost (focus loss, the node went away): keep and commit what
		// the drag reached.
		virtual void OnPointerCancel(UiControlContext& context, bool captured) {}

		virtual void OnPointerEnter(UiControlContext& context) {}

		virtual bool OnKey(UiControlContext& context, UiKey key, UiKeyModifiers modifiers) { return false; }

		virtual UiActivation OnActivate(UiControlContext& context) { return UiActivation::Click; }

		virtual bool WantsWheel(const UiControlContext& context) const { return false; }

		virtual bool OnWheel(UiControlContext& context, UiPoint delta) { return false; }

		// Focus left this control (to newFocus, possibly empty).
		virtual void OnBlur(UiControlContext& context, UiNodeId newFocus) {}

		// Unfocusable controls (options): the node a press focuses instead.
		virtual UiNodeId GetFocusTarget(const UiControlContext& context) const { return {}; }

		// An editable part (a slider's typed value) lost focus or submitted.
		virtual void OnPartCommit(UiControlContext& context, UiNodeId part) {}

		// A popup this control opened (UiControlContext::OpenPopup) was laid out.
		virtual void OnPopupLaidOut(UiControlContext& context, UiNodeId popup) {}

		// --- Layout, visuals and animation ---
		// The rectangle (relative to the parent's content box) of a part this control places
		// itself (slider thumbs, scroll bar thumbs, toggle knobs); nullopt: normal flow.
		virtual std::optional<UiRect> PlacePart(
			const UiControlContext& context, UiNodeId part, UiPartRole role, float partValue, const UiRect& inner) const
		{
			return std::nullopt;
		}

		virtual void OnArranged(UiControlContext& context) {}

		// State flags the control adds to itself and its parts (Checked, Mixed, ReadOnly...).
		virtual UiState GetStateFlags(const UiControlContext& context) const;

		// Advances animations; true while still animating.
		virtual bool OnUpdate(UiControlContext& context, float seconds) { return false; }

		virtual bool IsAnimating(const UiControlContext& context) const { return false; }

	  private:

		friend class UiControlRegistry;
		friend class UiDocument;
		std::string typeName;

	};

	// The value of a check state in events and GetValue: 0, 1 or 0.5 (Mixed).
	inline float UiCheckValue(UiCheckState state)
	{
		return state == UiCheckState::Checked ? 1.0f : state == UiCheckState::Mixed ? 0.5f : 0.0f;
	}

} // namespace Swim::UI
