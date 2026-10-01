#include "Engine/Runtime/RuntimeConsoleOverlay.h"

#include "Engine/Input/InputSystem.h"
#include "Engine/Runtime/UiRuntime.h"
#include "Engine/Systems/UI/UiWidgets.h"

#include <algorithm>

namespace Engine
{

	namespace
	{

		namespace UI = Swim::UI;
		constexpr float TextSize = 15.0f;
		constexpr std::size_t VisibleLines = 22;

	} // namespace

	RuntimeConsoleOverlay::RuntimeConsoleOverlay(UiRuntime& uiValue, RuntimeConsole& consoleValue) : ui(uiValue), console(consoleValue)
	{
		document = ui.CreateDocument();
		UI::UiStyle rootStyle;
		rootStyle.Flow = UI::UiFlow::Overlay;
		document->SetStyle(document->GetRoot(), rootStyle);

		UI::UiStyle panelStyle;
		panelStyle.Absolute = true;
		panelStyle.AnchorMin = { 0.0f, 0.0f };
		panelStyle.AnchorMax = { 1.0f, 0.0f };
		panelStyle.Pivot = { 0.0f, 0.0f };
		panelStyle.Width = UI::UiLength::Percent(1.0f);
		panelStyle.Flow = UI::UiFlow::Column;
		panelStyle.Padding = { 12, 10, 12, 10 };
		panelStyle.Gap = 6;
		panelStyle.Background = UI::UiSrgbHex(0x05070b, 0.93f);
		panelStyle.BorderWidth = 2.0f;
		panelStyle.BorderColor = UI::UiSrgbHex(0x1f7aff, 0.9f);
		panelStyle.HitTest = true; // Clicks on the console never reach the game.
		panel = UI::CreateStyledNode(*document, document->GetRoot(), panelStyle);

		UI::UiStyle scrollStyle;
		scrollStyle.Width = UI::UiLength::Percent(1.0f);
		scrollStyle.Clip = true;
		scrollStyle.TextColor = UI::UiSrgbHex(0xd8dee9);
		scrollback = UI::CreateLabel(*document, panel, ui.GetMonoFonts(), "", TextSize, scrollStyle);

		UI::UiStyle rowStyle;
		rowStyle.Flow = UI::UiFlow::Row;
		rowStyle.Gap = 6;
		rowStyle.Width = UI::UiLength::Percent(1.0f);
		rowStyle.AlignItems = UI::UiAlign::Center;
		const auto row = UI::CreateStyledNode(*document, panel, rowStyle);
		UI::UiStyle promptStyle;
		promptStyle.TextColor = UI::UiSrgbHex(0x5ea2ff);
		UI::CreateLabel(*document, row, ui.GetMonoFonts(), ">", TextSize, promptStyle);
		UI::UiTextEditOptions options;
		options.MaxBytes = 512;
		field = UI::CreateTextField(*document, row, options);
		auto fieldStyle = document->GetStyle(field);
		fieldStyle.Grow = 1.0f;
		fieldStyle.Width = UI::UiLength::Percent(1.0f);
		document->SetStyle(field, fieldStyle);
		document->SetText(field, ui.GetMonoFonts(), "", TextSize);
		document->SetArrowNavigation(false); // Up/Down walk the history instead.
		document->On(field, UI::UiEventKind::Submit,
			[this](const UI::UiEvent&)
			{
				if (console.IsOpen())
				{
					const std::string line = GetInputText();
					SetInputText("");
					console.Execute(line);
				}

			});

		overlay = ui.AddOverlay(document, 100);
		RefreshScrollback();
	}

	RuntimeConsoleOverlay::~RuntimeConsoleOverlay()
	{
		document->ClearCallbacks(field);
		ui.RemoveOverlay(overlay);
	}

	void RuntimeConsoleOverlay::SetInputText(const std::string& text)
	{
		document->SetText(field, ui.GetMonoFonts(), text, TextSize);
		const auto end = static_cast<std::uint32_t>(text.size());
		document->SetSelection(field, { end, end });
	}

	std::string RuntimeConsoleOverlay::GetInputText() const
	{
		return document->GetText(field);
	}

	void RuntimeConsoleOverlay::SetOpen(bool open)
	{
		console.SetOpen(open);
		ui.SetOverlayVisible(overlay, open);
		SetInputText("");

		if (open)
		{
			document->Focus(field);
		}
		else
		{
			document->Focus({});
		}
	}

	void RuntimeConsoleOverlay::BeforeInput(const Swim::Input::InputSystem* input)
	{
		if (!input)
		{
			return;
		}

		using Swim::Platform::KeyCode;

		if (input->IsKeyTriggered(KeyCode::Grave))
		{
			SetOpen(!console.IsOpen());
			return;
		}

		if (!console.IsOpen())
		{
			return;
		}

		if (input->IsKeyTriggered(KeyCode::Escape))
		{
			SetOpen(false);
			return;
		}

		if (input->IsKeyTriggered(KeyCode::Up))
		{
			SetInputText(console.HistoryUp(GetInputText()));
		}
		else if (input->IsKeyTriggered(KeyCode::Down))
		{
			SetInputText(console.HistoryDown());
		}

		if (document->GetFocus() != field)
		{
			document->Focus(field); // Clicking the scrollback must not lose the input line.
		}
	}

	void RuntimeConsoleOverlay::AfterInput()
	{
		document->DispatchCallbacks();
		document->DrainEvents();

		if (console.IsOpen())
		{
			// The console key itself is text too: never keep it in the line.
			auto text = GetInputText();
			const auto before = text.size();
			std::erase_if(text,
				[](char c)
				{
					return c == '`' || c == '~';
				});

			if (text.size() != before)
			{
				SetInputText(text);
			}
		}

		RefreshScrollback();
	}

	void RuntimeConsoleOverlay::RefreshScrollback()
	{
		if (shownRevision == console.GetRevision())
		{
			return;
		}

		shownRevision = console.GetRevision();
		const auto& lines = console.GetLines();
		const std::size_t first = lines.size() > VisibleLines ? lines.size() - VisibleLines : 0;
		std::string text;

		for (std::size_t i = first; i < lines.size(); ++i)
		{
			text += lines[i];

			if (i + 1 < lines.size())
			{
				text += '\n';
			}
		}

		document->SetText(scrollback, ui.GetMonoFonts(), text, TextSize);
	}

} // namespace Engine
