#include "Engine/Systems/UI/UiDocument.h"
#include "Tests/Fixtures/TextFontFixture.h"
#include "Tests/Framework/Test.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

using namespace Swim::UI;

namespace
{
	std::size_t Count(const std::vector<UiEvent>& events, UiEventKind kind)
	{
		return std::count_if(events.begin(), events.end(),
			[=](const auto& event)
			{
				return event.Kind == kind;
			});
	}

	struct Field
	{
		UiDocument Ui;
		UiNodeId Node;
		std::shared_ptr<const Swim::Text::FontCollection> Fonts = Swim::Testing::LoadTextFontChain();

		explicit Field(bool multiline = false, float width = 400.0f, std::uint32_t maxBytes = 1u << 16)
		{
			Node = Ui.Create(Ui.GetRoot());
			UiStyle style;
			style.Width = UiLength::Pixels(width);
			style.Height = UiLength::Pixels(multiline ? 200.0f : 30.0f);
			style.Padding = { 4, 4, 4, 4 };
			style.Clip = true;
			Ui.SetStyle(Node, style);
			Ui.SetText(Node, Fonts, "", 20);
			UiTextEditOptions options;
			options.Multiline = multiline;
			options.MaxBytes = maxBytes;
			Ui.SetEditable(Node, true, options);
			Ui.Layout({ 800, 600 });
			Ui.Focus(Node);
		}

		std::uint32_t Caret() const { return Ui.GetSelection(Node).Caret; }

		void Key(UiKey key, bool shift = false, bool control = false)
		{
			UiKeyModifiers modifiers;
			modifiers.Shift = shift;
			modifiers.Control = control;
			SWIM_CHECK(Ui.KeyDown(key, modifiers));
		}
	};
} // namespace

SWIM_TEST("UI.Edit", "TypesMovesByClustersWordsAndLinesAndDeletes")
{
	Field field;
	auto& ui = field.Ui;
	SWIM_CHECK(ui.WantsTextInput());
	ui.TextInput("hello e\xCC\x81t\xC3\xA9 world");
	SWIM_CHECK_EQUAL(ui.GetText(field.Node), std::string("hello e\xCC\x81t\xC3\xA9 world"));
	SWIM_CHECK_EQUAL(Count(ui.DrainEvents(), UiEventKind::TextChanged), 1u);
	SWIM_CHECK_EQUAL(field.Caret(), 18u);
	field.Key(UiKey::Left, false, true); // Previous word start.
	SWIM_CHECK_EQUAL(field.Caret(), 13u);
	field.Key(UiKey::Left);
	field.Key(UiKey::Left); // Over "é" (2 bytes).
	SWIM_CHECK_EQUAL(field.Caret(), 10u);
	field.Key(UiKey::Left);
	field.Key(UiKey::Left); // Over "e" + combining acute as one cluster.
	SWIM_CHECK_EQUAL(field.Caret(), 6u);
	field.Key(UiKey::Right, true);
	SWIM_CHECK_EQUAL(ui.GetSelection(field.Node).Anchor, 6u);
	SWIM_CHECK_EQUAL(field.Caret(), 9u);
	field.Key(UiKey::Backspace); // Deletes the selected cluster.
	SWIM_CHECK_EQUAL(ui.GetText(field.Node), std::string("hello t\xC3\xA9 world"));
	field.Key(UiKey::Home);
	SWIM_CHECK_EQUAL(field.Caret(), 0u);
	field.Key(UiKey::Delete, false, true); // Word delete.
	SWIM_CHECK_EQUAL(ui.GetText(field.Node), std::string("t\xC3\xA9 world"));
	field.Key(UiKey::End);
	field.Key(UiKey::Backspace);
	SWIM_CHECK_EQUAL(ui.GetText(field.Node), std::string("t\xC3\xA9 worl"));
	field.Key(UiKey::A, false, true);
	ui.TextInput("replaced");
	SWIM_CHECK_EQUAL(ui.GetText(field.Node), std::string("replaced"));
	// Single-line fields drop line breaks and submit on Enter.
	ui.TextInput("\r\nx\ty\x01");
	SWIM_CHECK_EQUAL(ui.GetText(field.Node), std::string("replacedx\ty"));
	ui.DrainEvents();
	field.Key(UiKey::Enter);
	SWIM_CHECK_EQUAL(Count(ui.DrainEvents(), UiEventKind::Submit), 1u);
	SWIM_CHECK(!ui.KeyDown(UiKey::Space)); // Space arrives as text input.
	field.Key(UiKey::Up);
	SWIM_CHECK_EQUAL(field.Caret(), 0u); // Up/Down go to the ends of a single line.
	field.Key(UiKey::Down, true);
	SWIM_CHECK_EQUAL(field.Caret(), 11u);
	field.Key(UiKey::Escape); // Collapses the selection.
	SWIM_CHECK_EQUAL(ui.GetSelection(field.Node).Anchor, 11u);
}

SWIM_TEST("UI.Edit", "MultilineFieldsKeepColumnsAndLimitsCutAtClusters")
{
	Field field(true);
	auto& ui = field.Ui;
	ui.TextInput("first line");
	field.Key(UiKey::Enter);
	ui.TextInput("2nd");
	SWIM_CHECK_EQUAL(ui.GetText(field.Node), std::string("first line\n2nd"));
	ui.Layout({ 800, 600 });
	SWIM_CHECK_EQUAL(ui.GetTextLayout(field.Node)->GetLines().size(), 2u);
	const float column = ui.GetTextLayout(field.Node)->GetCaret(14).X;
	field.Key(UiKey::Up);
	const auto* lines = ui.GetTextLayout(field.Node);
	SWIM_CHECK_EQUAL(lines->GetLineIndex(field.Caret()), 0u);
	SWIM_CHECK(std::abs(lines->GetCaret(field.Caret()).X - column) < 10.0f); // The same column, one line up.
	field.Key(UiKey::End);
	SWIM_CHECK_EQUAL(field.Caret(), 10u);
	field.Key(UiKey::Down);
	SWIM_CHECK_EQUAL(field.Caret(), 14u);
	field.Key(UiKey::Home, false, true);
	SWIM_CHECK_EQUAL(field.Caret(), 0u);

	Field limited(false, 400.0f, 6);
	limited.Ui.TextInput("abcde\xCC\x81\xCC\x81xyz"); // "e" + two marks would exceed 6 bytes.
	SWIM_CHECK_EQUAL(limited.Ui.GetText(limited.Node), std::string("abcd"));
	limited.Ui.TextInput("\xF0\x9F\x98\x80");
	SWIM_CHECK_EQUAL(limited.Ui.GetText(limited.Node), std::string("abcd")); // 4-byte emoji does not fit.
	limited.Ui.TextInput("zz");
	SWIM_CHECK_EQUAL(limited.Ui.GetText(limited.Node), std::string("abcdzz"));
	SWIM_CHECK_THROWS(limited.Ui.SetEditable(limited.Node, true, { false, 0 }), std::invalid_argument);
}

SWIM_TEST("UI.Edit", "ClipboardAndCompositionStayOutOfCommittedTextUntilCommitted")
{
	Field field;
	auto& ui = field.Ui;
	std::string clipboard;
	ui.SetClipboard({ [&]
		{
			return clipboard;
		},
		[&](std::string_view text)
		{
			clipboard = std::string(text);
		} });
	ui.TextInput("copy me");
	field.Key(UiKey::Left, true, true); // Select "me".
	field.Key(UiKey::C, false, true);
	SWIM_CHECK_EQUAL(clipboard, std::string("me"));
	field.Key(UiKey::X, false, true);
	SWIM_CHECK_EQUAL(ui.GetText(field.Node), std::string("copy "));
	field.Key(UiKey::V, false, true);
	field.Key(UiKey::V, false, true);
	SWIM_CHECK_EQUAL(ui.GetText(field.Node), std::string("copy meme"));

	// A preedit is displayed at the caret, underlined, but not committed.
	ui.DrainEvents();
	ui.SetComposition("ka", 1);
	SWIM_CHECK_EQUAL(ui.GetText(field.Node), std::string("copy meme"));
	SWIM_CHECK_EQUAL(ui.GetComposition(), std::string("ka"));
	ui.Layout({ 800, 600 });
	SWIM_CHECK_EQUAL(ui.GetTextLayout(field.Node)->GetText(), std::string("copy memeka"));
	Swim::Text::GlyphAtlas atlas;
	const auto& paint = ui.Paint(atlas);
	std::size_t glyphs = 0;
	std::size_t solids = 0;
	for (const auto& quad : paint)
	{
		glyphs += quad.Kind == UiPaintKind::Glyph ? 1 : 0;
		solids += quad.Kind == UiPaintKind::Solid ? 1 : 0;
	}
	SWIM_CHECK_EQUAL(glyphs, 10u);		 // 8 committed letters + 2 preedit.
	SWIM_CHECK_EQUAL(solids, 2u);		 // Underline and caret.
	SWIM_CHECK(ui.KeyDown(UiKey::Left)); // The IME owns keys while composing.
	SWIM_CHECK_EQUAL(ui.GetComposition(), std::string("ka"));
	ui.TextInput("\xE3\x81\x8B"); // Commit replaces the preedit.
	SWIM_CHECK_EQUAL(ui.GetText(field.Node), std::string("copy meme\xE3\x81\x8B"));
	SWIM_CHECK(ui.GetComposition().empty());
	ui.SetComposition("x", 1);
	field.Key(UiKey::Escape);
	SWIM_CHECK(ui.GetComposition().empty());
	ui.SetComposition("y", 1);
	ui.Focus({}); // Focus loss abandons the preedit.
	SWIM_CHECK(ui.GetComposition().empty());
	SWIM_CHECK(!ui.WantsTextInput());
	SWIM_CHECK_EQUAL(Count(ui.DrainEvents(), UiEventKind::TextChanged), 1u);
}

SWIM_TEST("UI.Edit", "PointerPlacesAndDragsSelectionAndCaretScrollsIntoView")
{
	Field field(false, 120.0f);
	auto& ui = field.Ui;
	ui.Focus({});
	ui.Layout({ 800, 600 }, 2.0f);
	ui.TextInput("ignored without focus");
	SWIM_CHECK(ui.GetText(field.Node).empty());
	ui.SetText(field.Node, field.Fonts, "abcdef", 20);
	ui.Layout({ 800, 600 }, 2.0f);
	const auto* layout = ui.GetTextLayout(field.Node);
	const auto bounds = ui.GetBounds(field.Node);
	// Click between "b" and "c" (framebuffer pixels at DPI 2), then drag past the end.
	const float x = bounds.X + 4.0f + layout->GetCaret(2).X;
	ui.PointerDown({ x * 2.0f + 0.2f, (bounds.Y + 10.0f) * 2.0f });
	SWIM_CHECK(ui.GetFocus() == field.Node);
	SWIM_CHECK_EQUAL(field.Caret(), 2u);
	ui.PointerMove({ 2000.0f, (bounds.Y + 10.0f) * 2.0f });
	ui.PointerUp({ 2000.0f, (bounds.Y + 10.0f) * 2.0f });
	SWIM_CHECK_EQUAL(ui.GetSelection(field.Node).Anchor, 2u);
	SWIM_CHECK_EQUAL(field.Caret(), 6u);
	ui.Layout({ 800, 600 }, 2.0f);
	Swim::Text::GlyphAtlas atlas;
	const auto& paint = ui.Paint(atlas);
	SWIM_CHECK(paint.front().Kind == UiPaintKind::Solid); // The selection, before the glyphs.

	// Typing past the clipped width scrolls the caret into view.
	field.Key(UiKey::End);
	ui.TextInput(" and a much longer tail of text");
	ui.Layout({ 800, 600 }, 2.0f);
	SWIM_CHECK(ui.GetScroll(field.Node).X > 0.0f);
	const auto rect = ui.GetTextInputRect();
	SWIM_CHECK(rect.X >= bounds.X * 2.0f && rect.X <= (bounds.X + bounds.Width) * 2.0f);
	SWIM_CHECK_NEAR(rect.Height, ui.GetTextLayout(field.Node)->GetLines()[0].Height * 2.0f, 1e-3f);
	field.Key(UiKey::Home);
	ui.Layout({ 800, 600 }, 2.0f);
	SWIM_CHECK_NEAR(ui.GetScroll(field.Node).X, 0.0f, 1e-4f);
	ui.SetCaretVisible(false);
	std::size_t solids = 0;
	for (const auto& quad : ui.Paint(atlas))
	{
		solids += quad.Kind == UiPaintKind::Solid ? 1 : 0;
	}
	SWIM_CHECK_EQUAL(solids, 0u);
	ui.SetEditable(field.Node, false);
	SWIM_CHECK(!ui.GetFocus()); // Not focusable any more (the style is not).
}

SWIM_TEST("UI.Input", "WheelScrollsTheInnermostClippedNode")
{
	UiDocument ui;
	const auto outer = ui.Create(ui.GetRoot());
	UiStyle outerStyle;
	outerStyle.Width = UiLength::Pixels(100);
	outerStyle.Height = UiLength::Pixels(100);
	outerStyle.Clip = true;
	ui.SetStyle(outer, outerStyle);
	const auto content = ui.Create(outer);
	UiStyle tall;
	tall.Width = UiLength::Pixels(100);
	tall.Height = UiLength::Pixels(300);
	ui.SetStyle(content, tall);
	ui.Layout({ 400, 400 });
	SWIM_CHECK(ui.Wheel({ 50, 50 }, { 0, 40 }));
	SWIM_CHECK(ui.Wheel({ 50, 50 }, { 0, 1000 }));
	ui.Layout({ 400, 400 });
	SWIM_CHECK_NEAR(ui.GetScroll(outer).Y, 200.0f, 1e-4f);
	SWIM_CHECK(!ui.Wheel({ 50, 50 }, { 0, 10 }));	 // Already at the end.
	SWIM_CHECK(!ui.Wheel({ 300, 300 }, { 0, -10 })); // Nothing scrollable there.
	SWIM_CHECK(ui.Wheel({ 50, 50 }, { 0, -50 }));
	ui.PointerMove({ 50, 50 }); // Lays out again with the last canvas.
	SWIM_CHECK_NEAR(ui.GetBounds(content).Y, -150.0f, 1e-4f);
}
