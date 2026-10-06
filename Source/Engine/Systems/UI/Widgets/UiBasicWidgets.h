#pragma once

#include "Engine/Systems/UI/UiDocument.h"
#include "Engine/Systems/UI/UiTheme.h"

#include <memory>
#include <string>

// Non-interactive building blocks: panels, labels, text fields, images, rows, headings.
namespace Swim::UI
{

	struct UiPanelDesc
	{
		UiFlow Flow = UiFlow::Column;
	};

	struct UiLabelDesc
	{
		std::string Text;
		std::shared_ptr<const Text::FontCollection> Fonts;
		float Size = 16.0f;
		UiStyle Style;
		bool Themed = true;
	};

	struct UiTextFieldDesc
	{
		UiTextEditOptions Options;
		std::shared_ptr<const Text::FontCollection> Fonts;
		float Size = 16.0f;
		UiStyle Style;
		bool Themed = true;
	};

	struct UiImageDesc
	{
		UiImage Image;
		UiStyle Style;
	};

	// Themed helpers. Those that show text throw std::logic_error when the document's
	// theme has no fonts.
	UiNodeId CreatePanel(UiDocument& document, UiNodeId parent, UiFlow flow = UiFlow::Column);
	UiNodeId CreateLabel(UiDocument& document, UiNodeId parent, std::string text);
	// An editable, clipped text node.
	UiNodeId CreateTextField(UiDocument& document, UiNodeId parent, const UiTextEditOptions& options = {});

	// Unthemed helpers with explicit fonts and styles.
	UiNodeId CreateLabel(UiDocument& document, UiNodeId parent, std::shared_ptr<const Text::FontCollection> fonts, std::string text,
		float size, const UiStyle& style = {});
	UiNodeId CreateImage(UiDocument& document, UiNodeId parent, const UiImage& image, const UiStyle& style = {});
	UiNodeId CreateTextField(UiDocument& document, UiNodeId parent, std::shared_ptr<const Text::FontCollection> fonts, float size,
		const UiTextEditOptions& options = {}, const UiStyle& style = {});

	// Sets a node's text (theme label size and fonts unless given) only when it changed.
	void SetLabelText(UiDocument& document, UiNodeId node, const std::string& text);
	void SetLabelText(UiDocument& document, UiNodeId node, const std::string& text,
		const std::shared_ptr<const Swim::Text::FontCollection>& fonts, float size);

	// A node with an explicit style and the theme's paint of a class (the theme's layout
	// fields would otherwise override the style's sizes).
	UiNodeId CreateStyledNode(UiDocument& document, UiNodeId parent, const UiStyle& style, UiThemeClass paintClass = UiThemeClass::None);
	// A small section heading.
	UiNodeId CreateHeading(UiDocument& document, UiNodeId parent, const std::string& text);
	// A horizontal row container.
	UiNodeId CreateRow(UiDocument& document, UiNodeId parent, float gap = 6.0f);

} // namespace Swim::UI
