#pragma once

#include "Engine/Runtime/RuntimeConsole.h"
#include "Engine/Systems/UI/UiDocument.h"

#include <cstdint>
#include <memory>

namespace Swim::Input
{
	class InputSystem;
}

namespace Engine
{
	class UiRuntime;

	// The runtime console's UI: an engine overlay (UiRuntime::AddOverlay) across the top of
	// the screen with the scrollback (monospace, newest at the bottom) and an input line.
	//
	//   `  (grave / tilde)  open / close        Enter   run the line
	//   Escape              close               Up/Down command history
	//
	// While open its input line has focus, so UiRuntime reports the keyboard as captured and
	// gameplay input (fly camera, shortcuts, the ball shooter) stands back. Call
	// BeforeInput before UiRuntime::ApplyInput (toggle and history keys) and AfterInput after
	// it (runs submitted lines).
	class RuntimeConsoleOverlay
	{
	  public:
		RuntimeConsoleOverlay(UiRuntime& ui, RuntimeConsole& console);
		~RuntimeConsoleOverlay();
		RuntimeConsoleOverlay(const RuntimeConsoleOverlay&) = delete;
		RuntimeConsoleOverlay& operator=(const RuntimeConsoleOverlay&) = delete;

		void BeforeInput(const Swim::Input::InputSystem* input);
		void AfterInput();

		void SetOpen(bool open);

		bool IsOpen() const { return console.IsOpen(); }

		// Test hooks.
		const std::shared_ptr<Swim::UI::UiDocument>& GetDocument() const { return document; }

		Swim::UI::UiNodeId GetInputField() const { return field; }

		void SetInputText(const std::string& text);
		std::string GetInputText() const;

	  private:
		void RefreshScrollback();

		UiRuntime& ui;
		RuntimeConsole& console;
		std::shared_ptr<Swim::UI::UiDocument> document;
		std::uint32_t overlay = 0;
		Swim::UI::UiNodeId panel;
		Swim::UI::UiNodeId scrollback;
		Swim::UI::UiNodeId field;
		std::uint64_t shownRevision = UINT64_MAX;
	};
} // namespace Engine
