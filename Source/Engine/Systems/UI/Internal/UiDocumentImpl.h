#pragma once

// Private state of UiDocument, shared by its implementation units (tree/API,
// layout, paint, input and editing). Not part of the public UI contract.

#include "Engine/Systems/UI/UiDocument.h"
#include "Engine/Systems/UI/UiTheme.h"

#include <algorithm>
#include <array>
#include <cmath>
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
			// Control behaviour (UiControls.cpp).
			UiControl Control;
			UiNodeId PartOf; // The control this node is a part of.
			UiPartRole Role = UiPartRole::None;
			float PartValue = 0.0f;		 // Slider ticks: the marked value.
			float Knob = 0.0f;			 // Toggle: displayed knob position 0 .. 1 (eases towards the state).
			float ScrollActivity = 1e9f; // Overlay scroll bars: seconds since the last activity.
			bool ControlHidden = false;	 // Auto/overlay scroll bar without overflow: not painted, not hit.
			float ControlOpacity = 1.0f; // Overlay scroll bar fade.
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
		UiNodeId Root;
		UiNodeId Hover;
		UiNodeId Pressed;
		UiNodeId Focused;
		UiNodeId Selecting; // Editable node under a pointer drag selection.
		UiPoint Framebuffer;
		float Dpi = 1.0f;
		bool Dirty = true;
		std::uint64_t Revision = 0;
		std::uint32_t MeasuredNodes = 0;
		std::uint32_t RepaintedNodes = 0;
		std::vector<UiNodeId> Order;
		std::vector<UiPaintQuad> Quads;
		std::vector<UiEvent> Events;
		const Text::GlyphAtlas* PaintAtlas = nullptr;
		std::uint64_t PaintRevision = 0;
		std::uint64_t PaintedLayoutRevision = 0;
		// Control pointer drag (UiControls.cpp).
		UiNodeId Dragging;		 // The control under a thumb/knob drag.
		float DragGrab = 0.0f;	 // Pointer minus thumb start along the axis (logical units).
		float DragStart = 0.0f;	 // Pointer position along the axis at press.
		float PressValue = 0.0f; // Value (or check state) at press.
		bool DragMoved = false;
		// Scroll bar step button held down (repeats in Update).
		UiNodeId Stepping;
		float StepDirection = 0.0f;
		float StepHeld = 0.0f;
		float NextStep = 0.0f;
		bool ArrowNavigation = true;
		// Theme.
		std::shared_ptr<const UiTheme> Theme;
		std::array<UiClassStyle, static_cast<std::size_t>(UiThemeClass::Count)> Classes;
		std::string Composition;
		std::uint32_t CompositionCursor = 0;
		UiClipboard Clipboard;
		bool CaretVisible = true;

		Node& Get(UiNodeId id) { return Nodes.at(id.Value); }

		const Node& Get(UiNodeId id) const { return Nodes.at(id.Value); }

		void RequireLayout() const;
		std::size_t Depth(UiNodeId id) const;
		std::size_t Height(UiNodeId id) const;

		bool IsControl(const Node& node) const { return node.Control.Kind != UiControlKind::None; }

		bool IsHitTestable(const Node& node) const
		{
			return !node.ControlHidden && (node.Style.HitTest || node.Editable || IsControl(node));
		}

		bool IsFocusable(const Node& node) const
		{
			return !node.ControlHidden &&
				(node.Style.Focusable || node.Editable || (IsControl(node) && node.Control.Kind != UiControlKind::ScrollBar));
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

		// --- Controls (UiControls.cpp) ---
		void ValidateControl(const Node& node, const UiControl& control) const;
		float ClampValue(const UiControl& control, float value) const;
		// The rectangle (relative to the parent's content box) of a placed part, if any.
		std::optional<UiRect> PartGeometry(const Node& part, const UiRect& parentInner) const;
		// Refreshes scroll bars from their targets after an Arrange (values, thumbs,
		// visibility), re-arranging thumbs that moved.
		void SyncScrollBars();
		void ReArrange(Node& node, UiRect bounds);
		bool ControlPointerDown(Node& control, UiPoint logical);
		void ControlPointerMove(UiPoint logical);
		void ControlPointerUp(Node& control, bool inside);
		void EndDrag(bool commit);
		bool ControlKey(Node& control, UiKey key);
		bool ControlWheel(Node& control, UiPoint delta);
		// Input changes: clamps/snaps, emits ValueChanged (and ValueCommitted when commit).
		bool ChangeValue(Node& control, float value, bool commit);
		void Toggle(Node& control);
		float CheckValue(UiCheckState state) const;
		void MarkControlDirty(Node& control);
		void SyncValueLabel(Node& control);
		// A scroll bar's thumb track along its axis (after its step buttons), relative to its
		// content box: start and length.
		std::pair<float, float> ScrollTrack(const Node& bar, const UiRect& inner) const;
		void StepScrollBar(Node& bar, float direction);
		bool AnimateControls(float seconds);
		float Axis(const Node& control, UiPoint logical) const; // Pointer position along the control's axis.

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
