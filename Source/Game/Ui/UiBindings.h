#pragma once

#include "Engine/Systems/UI/UiDocument.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Swim::Text
{
	class FontCollection;
}

namespace Game
{
	// Small glue between a retained UiDocument and gameplay code: click handlers keyed by
	// node, value/check watchers polled once per frame (so values set from code and by
	// input both reach the handler exactly once per change), and text helpers that only
	// touch the document when the text actually changed.
	class UiBindings
	{
	  public:
		void OnClick(Swim::UI::UiNodeId node, std::function<void()> handler);
		// Called with the new value whenever GetValue(node) changes. The first Process only
		// records the initial state (it is not a change), so a binding never overwrites state
		// set elsewhere, e.g. by startup commands. The same holds for OnChecked and OnText.
		void OnValue(Swim::UI::UiNodeId node, std::function<void(float)> handler);
		// Checkboxes and toggles.
		void OnChecked(Swim::UI::UiNodeId node, std::function<void(bool)> handler);
		// Editable text.
		void OnText(Swim::UI::UiNodeId node, std::function<void(const std::string&)> handler);

		// Drains the document's events and polls every watcher.
		void Process(Swim::UI::UiDocument& document);
		void Clear();

		std::uint64_t GetClickCount() const { return clicks; }

	  private:
		struct ValueWatch
		{
			Swim::UI::UiNodeId Node;
			std::function<void(float)> Handler;
			float Last = 0.0f;
			bool Seen = false;
		};

		struct CheckWatch
		{
			Swim::UI::UiNodeId Node;
			std::function<void(bool)> Handler;
			bool Last = false;
			bool Seen = false;
		};

		struct TextWatch
		{
			Swim::UI::UiNodeId Node;
			std::function<void(const std::string&)> Handler;
			std::string Last;
			bool Seen = false;
		};

		std::unordered_map<std::uint64_t, std::function<void()>> clickHandlers;
		std::vector<ValueWatch> values;
		std::vector<CheckWatch> checks;
		std::vector<TextWatch> texts;
		std::uint64_t clicks = 0;
	};

	// Sets a node's text (theme label size and fonts unless given) only when it changed.
	void SetLabelText(Swim::UI::UiDocument& document, Swim::UI::UiNodeId node, const std::string& text);
	void SetLabelText(Swim::UI::UiDocument& document, Swim::UI::UiNodeId node, const std::string& text,
		const std::shared_ptr<const Swim::Text::FontCollection>& fonts, float size);

	// A node with an explicit style and the theme's paint of a class (the theme's layout
	// fields would otherwise override the style's sizes).
	Swim::UI::UiNodeId CreateStyledNode(Swim::UI::UiDocument& document, Swim::UI::UiNodeId parent, const Swim::UI::UiStyle& style,
		Swim::UI::UiThemeClass paintClass = Swim::UI::UiThemeClass::None);
	// A small section heading.
	Swim::UI::UiNodeId CreateHeading(Swim::UI::UiDocument& document, Swim::UI::UiNodeId parent, const std::string& text);
	// A horizontal row container.
	Swim::UI::UiNodeId CreateRow(Swim::UI::UiDocument& document, Swim::UI::UiNodeId parent, float gap = 6.0f);
} // namespace Game
