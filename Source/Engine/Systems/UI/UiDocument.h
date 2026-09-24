#pragma once

#include "Engine/Systems/Text/GlyphAtlas.h"

#include <memory>
#include <string>

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
		bool Absolute = false;
		UiPoint Offset;
		bool Clip = false;
		bool Visible = true;
		bool Enabled = true;
		bool HitTest = false;
		bool Focusable = false;
		UiColor Background;
		UiColor TextColor{ 1.0f, 1.0f, 1.0f, 1.0f };
	};

	struct UiNodeId
	{
		std::uint64_t Value = 0; // Process-unique, never reused after removal.

		explicit operator bool() const { return Value != 0; }

		bool operator==(const UiNodeId&) const = default;
	};

	enum class UiPaintKind : std::uint8_t
	{
		Solid,
		Glyph
	};

	struct UiPaintQuad
	{
		UiNodeId Node;
		UiPaintKind Kind = UiPaintKind::Solid;
		UiRect Bounds; // Logical units, top left origin, positive down.
		UiRect Clip;
		UiColor Color; // Premultiplied linear RGBA.
		std::uint32_t AtlasPage = Text::NoAtlasPage;
		UiRect Uv;					// Normalized, top-down atlas coordinates.
		float DistanceRange = 0.0f; // Atlas texels; renderer uses derivatives for screen coverage.
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
		Cancel
	};

	struct UiEvent
	{
		UiEventKind Kind;
		UiNodeId Node;
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
		void SetStyle(UiNodeId node, const UiStyle& style);
		const UiStyle& GetStyle(UiNodeId node) const;
		// One script/direction run per hard line; CRLF/LF are supported. No implicit
		// fallback, paragraph bidi or wrapping yet. null face clears the text.
		void SetText(UiNodeId node, std::shared_ptr<const Text::FontFace> face, std::string text, float size,
			Text::TextDirection direction = Text::TextDirection::Auto);
		void SetScroll(UiNodeId node, UiPoint offset); // Clamped to content on the next Layout.
		UiPoint GetScroll(UiNodeId node) const;
		void Layout(UiPoint framebufferSize, float dpiScale = 1.0f);
		UiRect GetBounds(UiNodeId node) const; // Requires Layout after mutations.
		std::uint64_t GetLayoutRevision() const;
		const std::vector<UiPaintQuad>& Paint(Text::GlyphAtlas& atlas); // Requires current Layout.

		// Input uses framebuffer pixels. Returned events are queued, not callbacks:
		// consumers may mutate the document safely after DrainEvents().
		UiNodeId HitTest(UiPoint framebufferPoint) const;
		void PointerMove(UiPoint framebufferPoint);
		void PointerDown(UiPoint framebufferPoint);
		void PointerUp(UiPoint framebufferPoint);
		void CancelPointer();	   // Platform focus loss / pointer cancellation.
		void Focus(UiNodeId node); // Empty clears focus; unavailable targets are rejected.
		void FocusNext(bool backwards = false);
		void ActivateFocused(); // Map Enter/Space to this at the input adapter.
		UiNodeId GetFocus() const;
		std::vector<UiEvent> DrainEvents();

	  private:
		struct Impl;
		std::unique_ptr<Impl> impl;
	};
} // namespace Swim::UI
