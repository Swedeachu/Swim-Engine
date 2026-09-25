#pragma once

#include "Engine/Input/InputSystem.h"
#include "Engine/Systems/UI/UiDocument.h"

namespace Swim::UI
{
	enum class UiShortcutModifier : std::uint8_t
	{
		Control, // Windows, Linux.
		Super	 // macOS Command.
	};

	struct UiInputBridgeDesc
	{
		// Framebuffer pixels per window coordinate (the window's pixel density), since
		// UiDocument input is in framebuffer pixels and mouse positions are not.
		float FramebufferScale = 1.0f;
		float WheelStep = 48.0f; // Logical units scrolled per wheel notch.
		UiShortcutModifier Shortcut = UiShortcutModifier::Control;
		// Tab starts focus traversal even when no UI node has focus.
		bool TabStartsNavigation = true;
	};

	struct UiInputFrame
	{
		bool PointerOverUi = false;	   // A hit-testable node is under the pointer: keep it from the game.
		bool KeyboardCaptured = false; // A node has focus: keyboard input belongs to the UI.
		bool WantsTextInput = false;   // An editable node has focus: start platform text input.
		std::uint32_t KeysConsumed = 0;
	};

	// Adapts one accepted Input::InputSystem frame to a UiDocument (critical-path item 79):
	// pointer move/press/release of the primary button in framebuffer pixels, wheel
	// scrolling, key presses (including repeats) as UiKeys with modifiers, committed text
	// and IME composition. Application focus loss cancels pointer capture and clears UI
	// focus, as the document's input contract asks. Call once per frame after
	// InputSystem::AdvanceFrame and a Layout of the document; the platform calls
	// (Start/StopTextInput, SetTextInputArea) stay with the application.
	class UiInputBridge
	{
	  public:
		// Throws std::invalid_argument for a non-positive scale or wheel step.
		explicit UiInputBridge(UiInputBridgeDesc desc = {});

		UiInputFrame Apply(const Input::InputSystem& input, UiDocument& document);

	  private:
		UiInputBridgeDesc desc;
		bool hadFocus = true;
		bool hasPointer = false;
		UiPoint lastPointer;
	};
} // namespace Swim::UI
