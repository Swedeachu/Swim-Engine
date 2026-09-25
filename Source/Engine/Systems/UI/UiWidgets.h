#pragma once

#include "Engine/Systems/UI/UiDocument.h"

#include <span>
#include <unordered_map>

// Thin widget layer over UiDocument nodes (critical-path item 79). Widgets are ordinary
// nodes with a conventional style; everything stays reachable through the document.
namespace Swim::UI
{
	struct UiButtonColors
	{
		UiColor Normal{ 0.16f, 0.18f, 0.22f, 1.0f };
		UiColor Hover{ 0.22f, 0.25f, 0.31f, 1.0f };
		UiColor Pressed{ 0.10f, 0.12f, 0.15f, 1.0f };
		UiColor Focus{ 0.45f, 0.65f, 1.0f, 1.0f }; // Border color while focused.
		float FocusBorder = 2.0f;
	};

	// Labels do not take input. Style defaults to Auto size.
	UiNodeId CreateLabel(UiDocument& document, UiNodeId parent, std::shared_ptr<const Text::FontCollection> fonts, std::string text,
		float size, const UiStyle& style = {});
	// A hit-testable, focusable node with centered text, padding and rounded corners.
	UiStyle DefaultButtonStyle(const UiButtonColors& colors = {});
	UiNodeId CreateButton(UiDocument& document, UiNodeId parent, std::shared_ptr<const Text::FontCollection> fonts, std::string label,
		float size, const UiStyle& style = DefaultButtonStyle());
	UiNodeId CreateImage(UiDocument& document, UiNodeId parent, const UiImage& image, const UiStyle& style = {});
	// A clipped column: children scroll with UiDocument::Wheel/SetScroll.
	UiNodeId CreateScrollView(UiDocument& document, UiNodeId parent, const UiStyle& style);
	// An editable, clipped text node.
	UiNodeId CreateTextField(UiDocument& document, UiNodeId parent, std::shared_ptr<const Text::FontCollection> fonts, float size,
		const UiTextEditOptions& options = {}, const UiStyle& style = {});

	// Hover/pressed/focus visuals of tracked buttons, driven by the document's events as
	// paint-only style changes (no relayout).
	class UiButtonStates
	{
	  public:
		void Track(UiNodeId button, const UiButtonColors& colors = {});
		void Untrack(UiNodeId button);
		// Feed every drained event batch; untracked nodes and removed nodes are ignored.
		void Apply(UiDocument& document, std::span<const UiEvent> events);

	  private:
		struct State
		{
			UiButtonColors Colors;
			bool Hovered = false;
			bool Pressed = false;
			bool Focused = false;
		};

		void Refresh(UiDocument& document, UiNodeId button, const State& state) const;
		std::unordered_map<std::uint64_t, State> buttons;
	};
} // namespace Swim::UI
