#pragma once

// Private state of UiDocument, shared by its implementation units (tree/API,
// layout, paint, input and editing). Not part of the public UI contract.

#include "Engine/Systems/UI/UiDocument.h"

#include <algorithm>
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
		std::string Composition;
		std::uint32_t CompositionCursor = 0;
		UiClipboard Clipboard;
		bool CaretVisible = true;

		Node& Get(UiNodeId id) { return Nodes.at(id.Value); }

		const Node& Get(UiNodeId id) const { return Nodes.at(id.Value); }

		void RequireLayout() const;
		std::size_t Depth(UiNodeId id) const;
		std::size_t Height(UiNodeId id) const;

		bool IsHitTestable(const Node& node) const { return node.Style.HitTest || node.Editable; }

		bool IsFocusable(const Node& node) const { return node.Style.Focusable || node.Editable; }

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

		// --- Editing (UiEditing.cpp) ---
		UiTextSelection ClampSelection(Node& node, UiTextSelection selection);
		void ReplaceSelection(Node& node, std::string_view text);
		void SetCaret(Node& node, std::uint32_t caret, bool extend);
		bool EditKey(Node& node, UiKey key, UiKeyModifiers modifiers);
		std::uint32_t TextHit(Node& node, UiPoint logical);
		void EndComposition();
	};
} // namespace Swim::UI
