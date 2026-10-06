#pragma once

// Private state of UiDocument, shared by its implementation units (tree/API,
// layout, paint, input and editing). Not part of the public UI contract.

#include "Engine/Systems/UI/UiControlBehavior.h"
#include "Engine/Systems/UI/UiDocument.h"
#include "Engine/Systems/UI/UiTheme.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <unordered_map>
#include <vector>

namespace Swim::UI
{

	namespace Internal
	{

		constexpr float MaxLogical = 1000000.0f;
		constexpr std::size_t MaxDepth = 256;
		constexpr std::size_t MaxNodes = 65536;
		constexpr float Unbounded = std::numeric_limits<float>::infinity();

		inline UiRect Intersect(UiRect a, UiRect b)
		{
			const float x = std::max(a.X, b.X);
			const float y = std::max(a.Y, b.Y);
			return { x, y, std::max(0.0f, std::min(a.X + a.Width, b.X + b.Width) - x),
				std::max(0.0f, std::min(a.Y + a.Height, b.Y + b.Height) - y) };
		}

		inline UiColor Premultiply(UiColor color)
		{
			return { color.R * color.A, color.G * color.A, color.B * color.A, color.A };
		}

		// The box inside the padding; never negative.
		inline UiRect ContentBox(const UiRect& bounds, const UiEdges& padding)
		{
			return { bounds.X + padding.Left, bounds.Y + padding.Top, std::max(0.0f, bounds.Width - padding.Left - padding.Right),
				std::max(0.0f, bounds.Height - padding.Top - padding.Bottom) };
		}

		inline bool SameRect(const UiRect& a, const UiRect& b)
		{
			return a.X == b.X && a.Y == b.Y && a.Width == b.Width && a.Height == b.Height;
		}

		void ValidateStyle(const UiStyle& style);
		void ValidateVisual(const UiVisual& visual);
		void ValidateImage(const UiImage& image);
		// True when the two styles differ only in paint properties.
		bool OnlyPaintChanged(const UiStyle& before, const UiStyle& after);

	} // namespace Internal

	struct UiDocument::Impl
	{
		struct Node
		{
			UiNodeId Id;
			UiNodeId Parent;
			std::vector<UiNodeId> Children;
			UiStyle Style;
			// Text.
			std::shared_ptr<const Text::FontCollection> Fonts;
			std::string TextContents;
			float FontSize = 16.0f;
			UiTextOptions TextOptions;
			std::shared_ptr<const Text::TextLayout> TextLayout; // Arranged (displayed) layout.
			std::shared_ptr<const Text::TextLayout> MeasureLayout;
			// Image.
			bool HasImage = false;
			UiImage Image;
			// Editing.
			bool Editable = false;
			UiTextEditOptions EditOptions;
			UiTextSelection Selection;
			float PreferredCaretX = std::numeric_limits<float>::quiet_NaN();
			bool RevealCaret = false;
			// Callbacks (UiCallbacks.cpp) and the value binding (UiBindings.cpp).
			// Shared so dispatch can hold a handler while it replaces itself (no copy of the function).
			std::unordered_map<UiEventKind, std::shared_ptr<const std::function<void(const UiEvent&)>>> Callbacks;
			struct ValueBinding
			{
				std::function<float()> Get;
				std::function<void(float)> Set;
			};
			std::unique_ptr<ValueBinding> Binding;
			// Control (UiControlHost.cpp): the shared data and the behaviour that reacts to input.
			UiControl Control;
			std::unique_ptr<UiControlBehavior> Behavior;
			UiNodeId PartOf; // The control this node is a part of.
			UiPartRole Role = UiPartRole::None;
			float PartValue = 0.0f;		   // Slider ticks: the marked value. Options: their index.
			std::vector<UiNodeId> Options; // Selection owners: registered option nodes.
			UiNodeId Tooltip;
			float TooltipDelay = 0.5f;
			UiNodeId ContextMenu;
			bool ControlHidden = false;	 // Set by the control (auto scroll bar without overflow): not painted, not hit.
			float ControlOpacity = 1.0f; // Set by the control (overlay scroll bar fade).
			// Theme and visual states (UiVisuals.cpp).
			UiThemeClass ThemeClass = UiThemeClass::None;
			UiThemeApply ThemeApply = UiThemeApply::None;
			std::vector<UiStateRule> Rules;
			bool VisualDirty = true;
			bool HasVisual = false;
			UiState LastState = UiState::None;
			UiResolvedVisual Visual; // Displayed.
			UiResolvedVisual TransitionFrom;
			UiResolvedVisual TransitionTo;
			float TransitionElapsed = 0.0f;
			float TransitionDuration = 0.0f;
			bool Transitioning = false;
			float EffectiveOpacity = 1.0f; // Product along ancestors (Paint).
			float PaintedOpacity = -1.0f;
			// Layout results.
			UiPoint TextSize;
			UiPoint Desired;
			UiPoint Scroll;
			UiPoint ArrangedScroll; // The offset Bounds of descendants were arranged with.
			UiPoint MaxScroll;
			UiRect Bounds;
			UiRect Clip;
			bool Active = false;
			// Measure cache: valid while neither this node nor a descendant changed and
			// the inputs are the same.
			bool MeasureDirty = true;
			bool SubtreeDirty = true;
			UiPoint CachedAvailable{ -1.0f, -1.0f };
			float CachedWrap = -1.0f;
			// Paint cache.
			std::vector<UiPaintQuad> Paint;
			bool PaintDirty = true;
			UiRect PaintedBounds;
			UiRect PaintedClip;
			UiPoint PaintedScroll;
		};

		std::unordered_map<std::uint64_t, Node> Nodes;
		UiDocument* Owner = nullptr; // Control contexts reach the document through it.
		UiNodeId Root;
		UiNodeId Hover;
		UiNodeId Pressed;
		UiNodeId Focused;
		UiNodeId Selecting; // Editable node under a pointer drag selection.
		UiNodeId Dragging;	// The control holding the pointer capture (UiPointerResponse::Capture).
		UiPoint Framebuffer;
		float Dpi = 1.0f;
		bool Dirty = true;
		std::uint64_t Revision = 0;
		std::uint32_t MeasuredNodes = 0;
		std::uint32_t RepaintedNodes = 0;
		std::vector<UiNodeId> Order;
		std::vector<UiPaintQuad> Quads;
		std::vector<UiEvent> Events;
		std::vector<UiEvent> CallbackEvents;
		std::vector<UiEvent> DispatchScratch; // Reused by DispatchCallbacks.
		bool DispatchingCallbacks = false;

		void QueueEvent(UiEvent event);

		const Text::GlyphAtlas* PaintAtlas = nullptr;
		std::uint64_t PaintRevision = 0;
		std::uint64_t PaintedLayoutRevision = 0;
		bool ArrowNavigation = true;
		// Controls whose behaviour asked for OnUpdate / OnArranged, and nodes with a value
		// binding: the per-frame loops walk these, never every node.
		std::vector<UiNodeId> UpdatedControls;
		std::vector<UiNodeId> ArrangedControls;
		std::vector<UiNodeId> BoundNodes;
		// Visual states: ResolveVisuals does nothing on frames where no node's state inputs
		// changed (no dirty node, same hover/press/focus/capture).
		bool VisualsDirty = true;
		UiNodeId ResolvedHover, ResolvedPressed, ResolvedFocused, ResolvedDragging;
		std::uint32_t TransitioningCount = 0; // Nodes easing a state change.

		// Popups, bottom to top.
		struct PopupEntry
		{
			UiNodeId Node;
			UiPopupDesc Desc;
			UiNodeId PriorFocus;
			bool PendingFocus = false;
			bool PendingNotify = false; // The anchor control opened it: OnPopupLaidOut after the next Layout.
		};

		std::vector<PopupEntry> Popups;
		// Pointer tracking for tooltips (logical units).
		UiPoint LastPointer;
		bool HasPointer = false;
		UiNodeId TooltipTarget; // The node whose tooltip the pointer rests on.
		float TooltipTime = 0.0f;
		bool TooltipSuppressed = false; // Pressed or scrolled: no tooltip until the target changes.
		UiNodeId TooltipShown;
		// Theme.
		std::shared_ptr<const UiTheme> Theme;
		std::unordered_map<UiThemeClass, UiClassStyle> Classes;
		std::string Composition;
		std::uint32_t CompositionCursor = 0;
		UiClipboard Clipboard;
		bool CaretVisible = true;

		Node& Get(UiNodeId id) { return Nodes.at(id.Value); }

		const Node& Get(UiNodeId id) const { return Nodes.at(id.Value); }

		Node* Find(UiNodeId id)
		{
			const auto found = Nodes.find(id.Value);
			return found == Nodes.end() ? nullptr : &found->second;
		}

		const Node* Find(UiNodeId id) const
		{
			const auto found = Nodes.find(id.Value);
			return found == Nodes.end() ? nullptr : &found->second;
		}

		UiControlContext Context(UiNodeId id) const { return UiControlContext(*Owner, id); }

		void RequireLayout() const;

		std::size_t Depth(UiNodeId id) const;

		std::size_t Height(UiNodeId id) const;

		bool IsControl(const Node& node) const { return node.Behavior != nullptr; }

		bool IsHitTestable(const Node& node) const
		{
			return !node.ControlHidden && (node.Style.HitTest || node.Editable || IsControl(node));
		}

		bool IsFocusable(const Node& node) const
		{
			return !node.ControlHidden &&
				(node.Style.Focusable || node.Editable || (node.Behavior && node.Behavior->IsFocusable()));
		}

		// Hit-testable, focusable, editable or a control: the owner of descendants' states.
		bool IsInteractive(const Node& node) const
		{
			return node.Style.HitTest || node.Style.Focusable || node.Editable || IsControl(node);
		}

		bool Available(UiNodeId id) const;

		void ClearUnavailable();

		void Erase(UiNodeId id);

		// Measure invalidation of a node and its ancestors (and a pending Layout).
		void MarkLayoutDirty(UiNodeId id);

		void MarkPaintDirty(UiNodeId id);

		void MarkVisualDirty(Node& node)
		{
			node.VisualDirty = true;
			VisualsDirty = true;
		}

		// --- Text (UiLayout.cpp) ---
		// The displayed text: committed text with the IME preedit at the caret.
		std::string DisplayText(const Node& node) const;

		bool IsComposing(const Node& node) const { return node.Id == Focused && node.Editable && !Composition.empty(); }

		std::shared_ptr<const Text::TextLayout> BuildTextLayout(const Node& node, float box) const;

		// A layout of the current display text for editing queries between Layouts.
		const Text::TextLayout& EditLayout(Node& node);

		// --- Layout (UiLayout.cpp) ---
		UiPoint Measure(Node& node, UiPoint available, float wrap);

		void Arrange(Node& node, UiRect bounds, UiRect clip, bool enabled);

		void ArrangeChildren(Node& node, const UiRect& inner, UiPoint& extent, std::vector<std::pair<UiNodeId, UiRect>>& placed);

		void RevealCaret(Node& node, const UiRect& inner);

		// --- Paint (UiPaint.cpp) ---
		void BuildPaint(Node& node, Text::GlyphAtlas& atlas);

		// --- Controls (UiControlHost.cpp): generic plumbing; behaviour lives in the controls ---
		// Validates and applies a control (behaviour + data) to a node; a null behaviour removes it.
		void ApplyControl(UiNodeId id, std::unique_ptr<UiControlBehavior> behavior, const UiControl& control);

		void ValidateControl(const Node& node, const UiControlBehavior& behavior, const UiControl& control) const;

		void DetachControl(Node& node);

		// Input changes: clamps through the behaviour, emits ValueChanged (and ValueCommitted when commit).
		bool ChangeValue(Node& control, float value, bool commit);

		void MarkControlDirty(Node& control);

		// The rectangle (relative to the parent's content box) of a placed part, if any.
		std::optional<UiRect> PartGeometry(const Node& part, const UiRect& parentInner) const;

		// After an Arrange: OnArranged of the controls that asked (scroll bars follow targets).
		void NotifyArranged();

		void ReArrange(Node& node, UiRect bounds);

		void PlacePartNow(UiNodeId part);

		// Ends a pointer capture: the behaviour keeps and commits what it reached.
		void CancelCapture();

		bool AnimateControls(float seconds);

		// Value bindings: pulls bound values into their controls (UiBindings.cpp).
		void PullBindings();

		void PushBinding(const UiEvent& event);

		// --- Options (selection owners) ---
		std::uint32_t OptionCount(const Node& owner) const;

		UiNodeId OptionFor(const Node& owner, std::int32_t index) const;

		// --- Popups (UiPopups.cpp) ---
		std::optional<std::size_t> PopupIndexOf(UiNodeId id) const; // The open popup containing a node.

		std::optional<std::size_t> TopModal() const;

		bool InputAllowed(UiNodeId id) const; // Outside a modal's reach: false.

		void ShowPopup(Node& node, bool visible);

		void ClosePopupsFrom(std::size_t index);

		// Opens (or moves to the top) a popup; notifyAnchor: the anchor control's OnPopupLaidOut
		// runs after the next Layout.
		void OpenPopupEntry(UiNodeId id, const UiPopupDesc& desc, bool notifyAnchor);

		void PlacePopups();

		void MoveToTop(UiNodeId id); // Moves a subtree's Order range to the end (painted last).

		// Closes light-dismiss popups that do not contain the point (or their anchor).
		void LightDismiss(UiPoint logical);

		void CloseOnActivate(UiNodeId activated);

		// Before a popup opens: closes the light-dismiss popups above the one holding its
		// anchor (unrelated menus and lists), and the tooltip.
		void DismissForOpen(UiNodeId anchor, UiNodeId opening);

		void UpdateTooltips(float seconds);

		void HideTooltip(bool suppress);

		// --- Visual states and theme (UiVisuals.cpp) ---
		UiNodeId StateOwner(UiNodeId id) const;

		UiState ComputeState(UiNodeId id) const;

		UiResolvedVisual ResolveTarget(const Node& node, UiState state) const;

		// Resolves every node whose state or style changed; starts transitions.
		void ResolveVisuals();

		bool AdvanceTransitions(float seconds);

		void ApplyTheme(Node& node);

		void MarkSubtreeVisualDirty(UiNodeId id);

		// --- Navigation (UiInput.cpp) ---
		std::vector<UiNodeId> TabOrder() const;

		// --- Editing (UiEditing.cpp) ---
		UiTextSelection ClampSelection(Node& node, UiTextSelection selection);

		void ReplaceSelection(Node& node, std::string_view text);

		void SetCaret(Node& node, std::uint32_t caret, bool extend);

		bool EditKey(Node& node, UiKey key, UiKeyModifiers modifiers);

		std::uint32_t TextHit(Node& node, UiPoint logical);

		void EndComposition();
	};

} // namespace Swim::UI
