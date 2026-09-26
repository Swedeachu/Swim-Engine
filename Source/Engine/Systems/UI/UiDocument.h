#pragma once

#include "Engine/Systems/Text/GlyphAtlas.h"
#include "Engine/Systems/Text/TextLayout.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Swim::UI
{
	struct UiPoint
	{
		float X = 0.0f;
		float Y = 0.0f;
	};

	struct UiRect
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Width = 0.0f;
		float Height = 0.0f;
		bool Contains(UiPoint point) const;
	};

	struct UiColor
	{
		float R = 0.0f;
		float G = 0.0f;
		float B = 0.0f;
		float A = 0.0f; // Input is straight alpha, linear RGB. Paint output is premultiplied.
	};

	struct UiEdges
	{
		float Left = 0.0f;
		float Top = 0.0f;
		float Right = 0.0f;
		float Bottom = 0.0f;
	};

	enum class UiUnit : std::uint8_t
	{
		Auto,
		Logical,
		Percent
	};

	struct UiLength
	{
		UiUnit Unit = UiUnit::Auto;
		float Value = 0.0f;

		static UiLength Pixels(float value) { return { UiUnit::Logical, value }; }

		static UiLength Percent(float value) { return { UiUnit::Percent, value }; } // 0..1 of parent's content size.
	};

	enum class UiFlow : std::uint8_t
	{
		Overlay,
		Row,
		Column
	};

	// Cross-axis placement of Row/Column children. Auto (AlignSelf) defers to the
	// parent's AlignItems, where Auto means Start. Stretch fills the parent's cross
	// size when the child's cross length is Auto.
	enum class UiAlign : std::uint8_t
	{
		Auto,
		Start,
		Center,
		End,
		Stretch
	};

	// Main-axis distribution of the free space left after Grow/Shrink.
	enum class UiJustify : std::uint8_t
	{
		Start,
		Center,
		End,
		SpaceBetween,
		SpaceAround,
		SpaceEvenly
	};

	struct UiStyle
	{
		UiLength Width;
		UiLength Height;
		UiPoint MinSize;
		UiPoint MaxSize{ 1000000.0f, 1000000.0f };
		UiEdges Margin;
		UiEdges Padding;
		UiFlow Flow = UiFlow::Column;
		float Gap = 0.0f;
		// Row/Column flex (the defaults keep children at their preferred sizes).
		float Grow = 0.0f;	 // Share of the parent's positive free space along its flow.
		float Shrink = 0.0f; // Share (weighted by preferred size) of an overflow to absorb.
		UiJustify Justify = UiJustify::Start;
		UiAlign AlignItems = UiAlign::Auto;
		UiAlign AlignSelf = UiAlign::Auto;
		// Width / height; 0 disables. An Auto axis follows the other one.
		float AspectRatio = 0.0f;
		// Absolute children: the anchor points are fractions of the parent's content box.
		// Equal anchors pin Pivot (a fraction of the node's own size) at AnchorMin + Offset;
		// different anchors on an axis stretch the node between them (minus margins).
		bool Absolute = false;
		UiPoint Offset;
		UiPoint AnchorMin;
		UiPoint AnchorMax;
		UiPoint Pivot;
		bool Clip = false;
		bool Visible = true;
		bool Enabled = true;
		bool HitTest = false;
		bool Focusable = false;
		UiColor Background;
		// Rounded background and an inner border; paint-only (no layout change).
		float CornerRadius = 0.0f;
		float BorderWidth = 0.0f;
		UiColor BorderColor;
		UiColor TextColor{ 1.0f, 1.0f, 1.0f, 1.0f };
		Text::TextAlign TextAlign = Text::TextAlign::Start;
		Text::TextWrap TextWrap = Text::TextWrap::None;
		float LineSpacing = 1.0f;
		// Editable text only.
		UiColor SelectionColor{ 0.25f, 0.45f, 0.95f, 0.45f };
		UiColor CaretColor{ 1.0f, 1.0f, 1.0f, 1.0f };
		// Paint-only: multiplies the paint of this node and its descendants (0 .. 1).
		float Opacity = 1.0f;
		// Paint-only: state changes (hover, press, focus, ...) ease the resolved paint
		// (colors, border, radius, opacity) over this many seconds through Update(); 0 snaps.
		float TransitionSeconds = 0.0f;
		// Keyboard/gamepad order: nodes with TabIndex > 0 come first in ascending order,
		// then TabIndex 0 in document order; negative values are skipped by Tab and
		// directional navigation (still focusable by pointer or Focus()).
		std::int32_t TabIndex = 0;
	};

	struct UiNodeId
	{
		std::uint64_t Value = 0; // Process-unique, never reused after removal.

		explicit operator bool() const { return Value != 0; }

		bool operator==(const UiNodeId&) const = default;
	};

	enum class UiImageFit : std::uint8_t
	{
		Stretch, // Fill the content box.
		Contain	 // Largest centered rectangle with the image's aspect ratio.
	};

	// An image resource the renderer resolves; the UI only passes the handles through
	// (for example BindlessResourceTable indices). Colors are sampled as premultiplied.
	struct UiImage
	{
		std::uint32_t Texture = 0;
		std::uint32_t Sampler = 0;
		UiPoint Size;				// Natural size in logical units (the node's intrinsic content size).
		UiRect Uv{ 0, 0, 1, 1 };	// Normalized source rectangle, top-down.
		UiEdges Slice;				// Nine-slice border widths on screen, logical units (all 0: one quad).
		UiEdges SliceUv;			// The same borders in the source, as fractions of the full texture.
		UiColor Tint{ 1, 1, 1, 1 }; // Straight alpha, linear.
		UiImageFit Fit = UiImageFit::Stretch;
	};

	// Interaction state of a node, taken from its nearest interactive ancestor-or-self
	// (hit-testable, focusable, editable or a control), so a control's parts and a
	// button's label follow the control. Disabled comes from the node's own availability.
	enum class UiState : std::uint16_t
	{
		None = 0,
		Hovered = 1u << 0,
		Pressed = 1u << 1,
		Focused = 1u << 2,
		Disabled = 1u << 3,
		Checked = 1u << 4,	// Checkbox/toggle on.
		Mixed = 1u << 5,	// Checkbox indeterminate.
		ReadOnly = 1u << 6, // Control with ReadOnly.
		Dragging = 1u << 7, // Slider/scroll bar thumb or toggle knob under a pointer drag.
	};

	constexpr UiState operator|(UiState a, UiState b)
	{
		return static_cast<UiState>(static_cast<std::uint16_t>(a) | static_cast<std::uint16_t>(b));
	}

	constexpr UiState operator&(UiState a, UiState b)
	{
		return static_cast<UiState>(static_cast<std::uint16_t>(a) & static_cast<std::uint16_t>(b));
	}

	constexpr bool HasState(UiState state, UiState flags)
	{
		return (state & flags) == flags;
	}

	// Paint properties a state rule overrides; unset fields keep the value below it.
	struct UiVisual
	{
		std::optional<UiColor> Background;
		std::optional<UiColor> BorderColor;
		std::optional<UiColor> TextColor;
		std::optional<UiColor> ImageTint;
		std::optional<float> BorderWidth;
		std::optional<float> CornerRadius;
		std::optional<float> Opacity;
		// A skin: this image is painted in the node's content box instead of (or without)
		// the node's own image. Skins never change layout (intrinsic size stays the node's).
		std::optional<UiImage> Image;
	};

	// Applies when the node's state has every When flag and none of the Unless flags.
	// Rules apply in order: the document theme's class rules first, then the node's own.
	struct UiStateRule
	{
		UiState When = UiState::None;
		UiState Unless = UiState::None;
		UiVisual Visual;
	};

	// The paint a node is drawn with: its style, then the matching rules, eased by
	// TransitionSeconds. Opacity is the node's own (not multiplied by ancestors).
	struct UiResolvedVisual
	{
		UiColor Background;
		UiColor BorderColor;
		UiColor TextColor{ 1.0f, 1.0f, 1.0f, 1.0f };
		UiColor ImageTint{ 1.0f, 1.0f, 1.0f, 1.0f };
		float BorderWidth = 0.0f;
		float CornerRadius = 0.0f;
		float Opacity = 1.0f;
		bool HasImage = false;
		UiImage Image;
	};

	// Behaviour the document implements for a node (critical-path item 79 controls).
	// Controls are ordinary nodes: they get pointer, wheel and keyboard input through the
	// document only, so they work the same on screen and world-space canvases.
	enum class UiControlKind : std::uint8_t
	{
		None,
		Button,	   // Click on release inside, Enter/Space.
		Checkbox,  // Unchecked/Checked/Mixed; click, Space or Enter.
		Toggle,	   // Off/on switch; click, knob drag or Space/Enter; the knob eases.
		Slider,	   // A value in [Min, Max]: thumb drag, track click, keys, wheel while focused.
		ScrollBar, // Shows and drives the scroll offset of ScrollTarget.
		// Selection owners: one selected index (Value, -1 for none) among Option nodes
		// registered with SetPartRole(option, owner, UiPartRole::Option, index).
		RadioGroup, // Arrows select the previous/next option (wrapping); a click selects.
		ListView,	// Up/Down/Page/Home/End select and scroll ScrollTarget to reveal; Enter submits.
		Dropdown,	// Activation opens Parts.Popup below it; Up/Down select while closed and
					// highlight while open; Enter commits the highlight; the Label shows the choice.
		Option,		// A choice of an owner (set by SetPartRole): hit-testable, not focusable.
	};

	enum class UiOrientation : std::uint8_t
	{
		Horizontal,
		Vertical // Sliders: Min at the bottom. Scroll bars: offset 0 at the top.
	};

	enum class UiCheckState : std::uint8_t
	{
		Unchecked,
		Checked,
		Mixed // Settable from code; a click turns it Checked.
	};

	enum class UiTrackClick : std::uint8_t
	{
		Jump, // The thumb jumps under the pointer and follows it.
		Page  // One PageStep towards the pointer.
	};

	enum class UiScrollBarVisibility : std::uint8_t
	{
		Always,
		Auto,	// Hidden (not painted, not hit) while the target does not overflow.
		Overlay // As Auto, and fades out after FadeDelaySeconds without scrolling or hover.
	};

	// The role of a control's part node; geometry roles are placed by the control.
	enum class UiPartRole : std::uint8_t
	{
		None,
		Track,	   // Slider: full length, centered across (placed). Toggle/checkbox: the box (flow).
		Fill,	   // Slider: from the value's start to the thumb center (placed).
		Thumb,	   // Slider/scroll bar: at the value (placed); toggle: the knob, inside the track.
		Mark,	   // Checkbox check mark (flow; shown by state rules).
		Mixed,	   // Checkbox indeterminate mark (flow; shown by state rules).
		Label,	   // Checkbox/toggle: text next to the control (flow). Slider: its value display.
		Decrement, // Scroll bar step button at the start (placed; press steps, holding repeats).
		Increment, // Scroll bar step button at the end.
		Tick,	   // Slider tick mark at a value (placed; UiDocument::SetPartRole).
		Option,	   // A choice of a radio group, list view or dropdown (UiDocument::SetPartRole).
	};

	// Part nodes must be descendants of the control, except a slider's value Label (any
	// node). Slider Track/Fill/Thumb and scroll bar Thumb/Decrement/Increment must be direct
	// children; a toggle's Thumb is a child of its Track.
	struct UiControlParts
	{
		UiNodeId Track;
		UiNodeId Fill;
		UiNodeId Thumb;
		UiNodeId Mark;
		UiNodeId Mixed;
		UiNodeId Label;
		UiNodeId Decrement;
		UiNodeId Increment;
		UiNodeId Popup; // Dropdown: its option list, a popup (a child of the root).
	};

	struct UiControl
	{
		UiControlKind Kind = UiControlKind::None;
		UiOrientation Orientation = UiOrientation::Horizontal;
		// Slider range and value (scroll bars mirror their target: Min 0, Max the maximum
		// scroll offset, Value the offset). Step > 0 snaps values to Min + k * Step.
		float Min = 0.0f;
		float Max = 1.0f;
		float Value = 0.0f;
		float Step = 0.0f;	   // 0: continuous; keys move 1 % of the range.
		float PageStep = 0.0f; // 0: 10 % of the range (scroll bars: the viewport length).
		UiCheckState Check = UiCheckState::Unchecked;
		bool ReadOnly = false; // Focusable and hoverable, but the value never changes by input.
		UiTrackClick TrackClick = UiTrackClick::Jump;
		// Scroll bars.
		UiNodeId ScrollTarget; // A clipped node; its MaxScroll/Scroll on this axis drive the bar.
		UiScrollBarVisibility Visibility = UiScrollBarVisibility::Auto;
		float MinThumbLength = 16.0f; // Logical units.
		float FadeDelaySeconds = 1.0f;
		float FadeSeconds = 0.3f;
		// Sliders: >= 0 makes the document write the value, with this many decimals, into
		// the Label part's text whenever it changes (the label needs fonts).
		std::int32_t LabelDecimals = -1;
		// List views and dropdowns: options of index i occupy [i, i + 1) x ItemExtent of the
		// ScrollTarget's content (virtualized lists whose rows exist only while visible);
		// 0 reveals options by their laid-out bounds. ItemCount > 0 overrides the number of
		// registered options (virtualized lists).
		float ItemExtent = 0.0f;
		std::uint32_t ItemCount = 0;
		UiControlParts Parts;
	};

	enum class UiPopupSide : std::uint8_t
	{
		Below, // Under Anchor (flipped above when it does not fit).
		Above,
		Right, // Beside Anchor (flipped to the left when it does not fit).
		Left,
		AtPoint, // At Point (canvas logical units), flipped up/left when it does not fit.
		Center,	 // Centered in the canvas.
	};

	// How UiDocument::OpenPopup places and dismisses a popup.
	struct UiPopupDesc
	{
		UiNodeId Anchor; // Placement reference; focus returns to it (or the prior focus) on close.
		UiPoint Point;
		UiPopupSide Side = UiPopupSide::Below;
		UiPoint Offset;				   // Added after placement (a gap).
		bool MatchAnchorWidth = false; // At least as wide as the anchor (dropdown lists).
		bool Modal = false;			   // Only this popup and those above it take input; Tab stays inside.
		bool LightDismiss = true;	   // A press outside it (and its anchor) closes it.
		bool CloseOnEscape = true;
		bool CloseOnActivate = false; // Clicking or activating a node inside closes it (menus).
		bool FocusFirst = true;		  // Focus its first focusable node on open.
	};

	enum class UiThemeClass : std::uint8_t
	{
		None,
		Panel,
		Label,
		Button,
		TextField,
		ScrollView,
		ScrollBar,
		ScrollThumb,
		Checkbox,
		CheckBox, // The checkbox's box.
		CheckMark,
		CheckMixed,
		Toggle,
		ToggleTrack,
		ToggleKnob,
		Slider,
		SliderTrack,
		SliderFill,
		SliderThumb,
		SliderTick,
		SliderValue,
		ScrollButton,
		Popup,		   // Menus, dropdown lists: a raised panel.
		MenuItem,	   // Menu entries and dropdown/list options.
		MenuSeparator, // A thin rule between menu entries.
		ListView,
		Dropdown,
		DropdownArrow,
		RadioOption, // A radio button row: [circle [dot], label].
		RadioCircle,
		RadioDot,
		Tooltip,
		ModalScrim, // The full-canvas dimmer behind a modal dialog.
		Dialog,
		DialogTitle,
		Count
	};

	// Which parts of a theme class a themed node takes (UiDocument::SetThemeClass).
	enum class UiThemeApply : std::uint8_t
	{
		None = 0,
		Paint = 1u << 0,  // Paint fields (colors, border, radius, opacity, transition) and the class's state rules.
		Layout = 1u << 1, // Width, Height, MinSize, Padding and Gap (axes swapped for vertical controls).
		Text = 1u << 2,	  // The theme's fonts and the class's text size, for nodes with text.
		All = Paint | Layout | Text
	};

	constexpr UiThemeApply operator|(UiThemeApply a, UiThemeApply b)
	{
		return static_cast<UiThemeApply>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
	}

	constexpr bool HasApply(UiThemeApply apply, UiThemeApply flag)
	{
		return (static_cast<std::uint8_t>(apply) & static_cast<std::uint8_t>(flag)) != 0;
	}

	class UiTheme;

	enum class UiNavDirection : std::uint8_t
	{
		Up,
		Down,
		Left,
		Right
	};

	enum class UiPaintKind : std::uint8_t
	{
		Solid,
		Glyph,
		Image
	};

	struct UiPaintQuad
	{
		UiNodeId Node;
		UiPaintKind Kind = UiPaintKind::Solid;
		UiRect Bounds; // Logical units, top left origin, positive down.
		UiRect Clip;
		UiColor Color; // Premultiplied linear RGBA (the tint for images).
		std::uint32_t AtlasPage = Text::NoAtlasPage;
		UiRect Uv;					// Normalized, top-down atlas/texture coordinates.
		float DistanceRange = 0.0f; // Atlas texels; renderer uses derivatives for screen coverage.
		// Solid: rounded corners and an inner border ring (premultiplied), logical units.
		float CornerRadius = 0.0f;
		float BorderWidth = 0.0f;
		UiColor BorderColor;
		// Image: renderer-defined handles from UiImage.
		std::uint32_t Texture = 0;
		std::uint32_t Sampler = 0;
	};

	enum class UiEventKind : std::uint8_t
	{
		Enter,
		Leave,
		Press,
		Release,
		Click,
		Focus,
		Blur,
		Cancel,
		TextChanged,	// An editable node's committed text changed.
		Submit,			// Enter in a single-line editable node.
		ValueChanged,	// A control's value or check state changed by input (Value: the new value).
		ValueCommitted, // The end of a value change: drag release, key, click (Value: the committed value).
		PopupOpened,	// Node: the popup.
		PopupClosed,	// Node: the popup (light dismiss, Escape, activation or ClosePopup).
		ContextMenu,	// Node: the target a context menu opened for (before PopupOpened).
	};

	struct UiEvent
	{
		UiEventKind Kind = UiEventKind::Enter;
		UiNodeId Node;
		// ValueChanged/ValueCommitted: the slider value, the scroll offset, or the check
		// state (0 unchecked, 1 checked, 0.5 mixed).
		float Value = 0.0f;
	};

	// Keys the document handles itself; the input adapter maps platform keys.
	enum class UiKey : std::uint8_t
	{
		Left,
		Right,
		Up,
		Down,
		Home,
		End,
		Backspace,
		Delete,
		Enter,
		Space,
		Tab,
		Escape,
		A,
		C,
		V,
		X,
		PageUp,
		PageDown
	};

	struct UiKeyModifiers
	{
		bool Shift = false;
		bool Control = false; // The platform's shortcut modifier (Command on macOS).
		bool Alt = false;
	};

	struct UiTextOptions
	{
		Text::TextDirection Direction = Text::TextDirection::Auto;
		std::string Language; // BCP 47.
	};

	struct UiTextEditOptions
	{
		bool Multiline = false;			   // Enter inserts a line break instead of submitting.
		std::uint32_t MaxBytes = 1u << 16; // Committed UTF-8 bytes (insertions beyond are truncated at a grapheme).
	};

	// A logical selection in byte offsets of GetText(); Anchor == Caret is a caret.
	struct UiTextSelection
	{
		std::uint32_t Anchor = 0;
		std::uint32_t Caret = 0;
	};

	struct UiClipboard
	{
		std::function<std::string()> Read;
		std::function<void(std::string_view)> Write;
	};

	// Retained document independent of Scene, Transform, SDL and any renderer.
	// Single-thread owner. The atlas passed to Paint must outlive its paint list.
	class UiDocument final
	{
	  public:
		UiDocument();
		~UiDocument();
		UiDocument(const UiDocument&) = delete;
		UiDocument& operator=(const UiDocument&) = delete;

		UiNodeId GetRoot() const;
		UiNodeId Create(UiNodeId parent);
		bool Remove(UiNodeId node); // Removes descendants; root removal returns false.
		bool Contains(UiNodeId node) const;
		void Reparent(UiNodeId node, UiNodeId parent); // Appends in paint/tab order; rejects cycles.
		// Paint-only changes (colors, corner radius, border) keep the cached layout.
		void SetStyle(UiNodeId node, const UiStyle& style);
		const UiStyle& GetStyle(UiNodeId node) const;
		// Paragraph text: bidi, script itemization, cluster fallback through the
		// collection, and the style's alignment/wrapping/line spacing. Invalid UTF-8 is
		// replaced with U+FFFD. A null collection clears the text.
		void SetText(UiNodeId node, std::shared_ptr<const Text::FontCollection> fonts, std::string text, float size,
			const UiTextOptions& options = {});
		// One face, no fallback (compatibility overload).
		void SetText(UiNodeId node, std::shared_ptr<const Text::FontFace> face, std::string text, float size,
			Text::TextDirection direction = Text::TextDirection::Auto);
		const std::string& GetText(UiNodeId node) const;
		// The node's current paragraph layout, in content-box coordinates (null without text).
		const Text::TextLayout* GetTextLayout(UiNodeId node) const;
		void SetImage(UiNodeId node, const UiImage& image);
		void ClearImage(UiNodeId node);
		void SetScroll(UiNodeId node, UiPoint offset); // Clamped to content on the next Layout.
		UiPoint GetScroll(UiNodeId node) const;
		void Layout(UiPoint framebufferSize, float dpiScale = 1.0f);
		UiRect GetBounds(UiNodeId node) const; // Requires Layout after mutations.
		bool IsLayoutCurrent() const;		   // False after a mutation until the next Layout.
		std::uint64_t GetLayoutRevision() const;
		// Nodes measured (not served from the measure cache) by the last Layout.
		std::uint32_t GetMeasuredNodeCount() const;
		// Rebuilds only the paint of nodes whose content, placement or atlas changed.
		const std::vector<UiPaintQuad>& Paint(Text::GlyphAtlas& atlas); // Requires current Layout.
		// Nodes whose paint was rebuilt by the last Paint.
		std::uint32_t GetRepaintedNodeCount() const;
		// Generates the atlas entries of every glyph the laid-out document shows, with
		// distance fields built through parallelFor (for example JobSystem::ParallelFor),
		// so a following Paint only looks glyphs up. Requires current Layout. Returns the
		// number of glyphs added.
		std::size_t PrewarmGlyphs(Text::GlyphAtlas& atlas, const Text::GlyphAtlas::ParallelFor& parallelFor = {});

		// Input uses framebuffer pixels. Returned events are queued, not callbacks:
		// consumers may mutate the document safely after DrainEvents(). HitTest requires a
		// current Layout; the pointer methods lay out again with the last canvas when needed.
		UiNodeId HitTest(UiPoint framebufferPoint) const;
		void PointerMove(UiPoint framebufferPoint);
		void PointerDown(UiPoint framebufferPoint, UiKeyModifiers modifiers = {});
		void PointerUp(UiPoint framebufferPoint);
		void CancelPointer(); // Platform focus loss / pointer cancellation.
		// Scrolls the innermost clipped, scrollable node under the point; true if it moved.
		bool Wheel(UiPoint framebufferPoint, UiPoint delta);
		// The pointer left this document (another canvas took it): hover ends. A pressed
		// node keeps its capture until PointerUp/CancelPointer.
		void PointerLeave();
		void Focus(UiNodeId node); // Empty clears focus; unavailable targets are rejected.
		// Tab order (see UiStyle::TabIndex); wraps around.
		void FocusNext(bool backwards = false);
		// Moves focus to the nearest focusable node in a direction (keyboard arrows,
		// gamepad D-pad); without focus, focuses the first node in tab order. False when
		// nothing lies that way (no wrap-around).
		bool Navigate(UiNavDirection direction);
		// Arrow keys that the focused node does not use navigate (default on).
		void SetArrowNavigation(bool enabled);
		// Enter/Space/gamepad A: toggles checkboxes and toggles, clicks anything else.
		void ActivateFocused();
		UiNodeId GetFocus() const;
		std::vector<UiEvent> DrainEvents();
		// Lays the document out again with the last canvas when it changed since the last
		// Layout (no-op before the first Layout).
		void EnsureLayout();

		// --- Controls (critical-path item 79). ---
		// Makes a node a control: it becomes hit-testable (and focusable, except scroll
		// bars without Style.Focusable); parts must be descendants (see UiControlParts).
		// The value is clamped and snapped. Kind None removes the behaviour. Throws
		// std::invalid_argument for an invalid range, step, parts or scroll target.
		void SetControl(UiNodeId node, const UiControl& control);
		const UiControl& GetControl(UiNodeId node) const;
		// From code: clamped and snapped, no events. Scroll bars set their target's offset;
		// selection owners take the selected index (-1: none), clamped to the options.
		void SetValue(UiNodeId node, float value);
		float GetValue(UiNodeId node) const;
		void SetChecked(UiNodeId node, UiCheckState state); // From code: no events.
		UiCheckState GetChecked(UiNodeId node) const;
		// Registers an extra part: slider Tick marks at `value` (direct children of the
		// slider), or Options of a selection owner at the integral index `value` (inside a
		// radio group or list view, anywhere for a dropdown; the node becomes an Option
		// control: hit-testable, never focusable). Registering an index again moves it (virtual
		// rows); UiPartRole::None releases it. Throws std::invalid_argument otherwise.
		void SetPartRole(UiNodeId part, UiNodeId control, UiPartRole role, float value = 0.0f);
		// The option node showing an index of a selection owner (empty when not bound).
		UiNodeId FindOption(UiNodeId owner, std::uint32_t index) const;
		std::uint32_t GetOptionCount(UiNodeId owner) const; // ItemCount, else the registered options.

		// --- Popups: menus, dropdown lists, tooltips, modal dialogs. ---
		// A popup is a child of the root that the document shows, places (after Layout, in
		// canvas logical units, kept inside the canvas), paints above everything else in
		// opening order and dismisses. Opening an open popup moves it to the top and
		// replaces its description. Throws std::invalid_argument for a node that is not a
		// child of the root, or an unknown anchor.
		void OpenPopup(UiNodeId popup, const UiPopupDesc& desc = {});
		bool ClosePopup(UiNodeId popup); // Also closes every popup opened after it.
		void CloseAllPopups();
		bool IsPopupOpen(UiNodeId popup) const;
		UiNodeId GetTopPopup() const;
		// Tooltips: after the pointer rests on target (or a descendant) for delaySeconds
		// (through Update), tooltip opens below the pointer; leaving, pressing or scrolling
		// closes it. An empty tooltip removes the registration.
		void SetTooltip(UiNodeId target, UiNodeId tooltip, float delaySeconds = 0.5f);
		// Context menus: OpenContextMenu opens the menu registered on the node under the
		// point (or its nearest ancestor) at the point; OpenContextMenuForFocus opens the
		// focused node's (or its ancestors') below it. False when there is none.
		void SetContextMenu(UiNodeId target, UiNodeId menu);
		bool OpenContextMenu(UiPoint framebufferPoint);
		bool OpenContextMenuForFocus();
		// Closes the light-dismiss popups, as a press outside all of them would (a press on
		// another canvas: UiCanvasRouter calls it).
		void DismissPopups();
		// Adjusts the scroll of clipped ancestors so the node's bounds are visible. Requires
		// a current Layout; the new offsets apply at the next Layout.
		void ScrollIntoView(UiNodeId node);

		// --- Visual states and theming. ---
		// Rules applied after the theme class's rules (per-node overrides).
		void SetStateRules(UiNodeId node, std::vector<UiStateRule> rules);
		const std::vector<UiStateRule>& GetStateRules(UiNodeId node) const;
		UiState GetState(UiNodeId node) const;
		// The paint the node is drawn with, as of the last Paint or Update.
		const UiResolvedVisual& GetVisual(UiNodeId node) const;
		// The document theme (a default dark theme without fonts initially). Changing it
		// re-applies every themed node. Null is rejected.
		void SetTheme(std::shared_ptr<const UiTheme> theme);
		const std::shared_ptr<const UiTheme>& GetTheme() const;
		// Themes a node now and on every SetTheme (UiThemeClass::None unthemes it, keeping
		// its current style).
		void SetThemeClass(UiNodeId node, UiThemeClass themeClass, UiThemeApply apply = UiThemeApply::All);
		UiThemeClass GetThemeClass(UiNodeId node) const;
		// Advances paint transitions, toggle knobs and overlay scroll bar fades by seconds
		// and resolves visual states. True while anything is still animating. Knob motion
		// needs a Layout afterwards (IsLayoutCurrent turns false).
		bool Update(float seconds);
		bool IsAnimating() const;
		// Changes whenever Paint's output changed (render surfaces redraw only then).
		std::uint64_t GetPaintRevision() const;

		// --- Editable text (critical-path item 79). ---
		// Editable nodes are focusable and hit-testable; they need text fonts. Carets move
		// by grapheme clusters, words (Control) and lines; Shift extends the selection.
		void SetEditable(UiNodeId node, bool editable, const UiTextEditOptions& options = {});
		bool IsEditable(UiNodeId node) const;
		UiTextSelection GetSelection(UiNodeId node) const;
		void SetSelection(UiNodeId node, UiTextSelection selection); // Snapped to caret stops.
		// Routes to the focused node: editing and caret keys for editable nodes, Tab focus
		// traversal and Enter/Space activation otherwise. True when consumed.
		bool KeyDown(UiKey key, UiKeyModifiers modifiers = {});
		// Committed text from the platform (after IME composition). Replaces the selection.
		void TextInput(std::string_view utf8);
		// IME preedit shown at the caret (underlined), not part of GetText(); empty ends
		// it. cursor is a byte offset in the preedit text.
		void SetComposition(std::string_view utf8, std::uint32_t cursor);
		const std::string& GetComposition() const;
		void SetClipboard(UiClipboard clipboard);
		// True while an editable node has focus: start platform text input.
		bool WantsTextInput() const;
		// The caret rectangle of the focused editable node in framebuffer pixels (IME
		// candidate window placement); empty otherwise. Requires current Layout.
		UiRect GetTextInputRect() const;
		// Caret blink phase; the application owns the timing.
		void SetCaretVisible(bool visible);

	  private:
		struct Impl;
		std::unique_ptr<Impl> impl;
	};
} // namespace Swim::UI
