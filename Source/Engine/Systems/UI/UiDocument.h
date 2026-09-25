#pragma once

#include "Engine/Systems/Text/GlyphAtlas.h"
#include "Engine/Systems/Text/TextLayout.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>

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
		TextChanged, // An editable node's committed text changed.
		Submit		 // Enter in a single-line editable node.
	};

	struct UiEvent
	{
		UiEventKind Kind = UiEventKind::Enter;
		UiNodeId Node;
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
		X
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
		void Focus(UiNodeId node); // Empty clears focus; unavailable targets are rejected.
		void FocusNext(bool backwards = false);
		void ActivateFocused(); // Map Enter/Space to this at the input adapter.
		UiNodeId GetFocus() const;
		std::vector<UiEvent> DrainEvents();

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
		// Pointer, wheel and focus traversal re-run Layout with the last canvas when the
		// document changed since (for example text typed earlier in the same frame).
		void EnsureLayout();

		struct Impl;
		std::unique_ptr<Impl> impl;
	};
} // namespace Swim::UI
