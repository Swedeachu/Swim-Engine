#pragma once

#include "Engine/Input/InputSystem.h"
#include "Engine/Systems/UI/UiCanvasRouter.h"
#include "Engine/Systems/UI/UiDocument.h"

#include <optional>
#include <span>

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
		// The gamepad that navigates the UI (none: gamepads are left to the game). D-pad
		// and left stick move focus (or adjust the focused slider along its axis), South
		// activates, East is Escape, the shoulders are Shift+Tab / Tab.
		std::optional<Platform::InputDeviceId> Gamepad;
		float StickThreshold = 0.6f;
		// Held directions repeat after RepeatDelaySeconds every RepeatIntervalSeconds (needs
		// Apply's deltaSeconds).
		float RepeatDelaySeconds = 0.4f;
		float RepeatIntervalSeconds = 0.08f;
	};

	struct UiInputFrame
	{
		bool PointerOverUi = false;	   // A hit-testable node is under the pointer: keep it from the game.
		bool KeyboardCaptured = false; // A node has focus: keyboard input belongs to the UI.
		bool WantsTextInput = false;   // An editable node has focus: start platform text input.
		bool GamepadCaptured = false;  // The UI has focus and a navigating gamepad: keep its buttons from the game.
		std::uint32_t KeysConsumed = 0;
	};

	// Adapts one accepted Input::InputSystem frame to a UiDocument or to every canvas of a
	// UiCanvasRouter (critical-path item 79): pointer move/press/release of the primary
	// button, wheel scrolling, key presses (including repeats) as UiKeys with modifiers,
	// committed text, IME composition and gamepad navigation. Application focus loss
	// cancels pointer capture and clears UI focus. Call once per frame after
	// InputSystem::AdvanceFrame and a Layout of the documents; the platform calls
	// (Start/StopTextInput, SetTextInputArea) stay with the application.
	class UiInputBridge
	{
	  public:
		// Throws std::invalid_argument for a non-positive scale, wheel step, threshold or
		// repeat timing.
		explicit UiInputBridge(UiInputBridgeDesc desc = {});

		// One screen document; pointer positions in framebuffer pixels.
		UiInputFrame Apply(const Input::InputSystem& input, UiDocument& document, float deltaSeconds = 0.0f);
		// Every canvas: the mouse is a viewport point and, with a camera, a world ray through
		// it (world panels, billboards); surfaceHits are the application's mesh hits on
		// render-surface canvases for this pointer.
		UiInputFrame Apply(const Input::InputSystem& input, UiCanvasRouter& router, const UiCameraView* camera = nullptr,
			std::span<const UiSurfaceHit> surfaceHits = {}, float deltaSeconds = 0.0f);

	  private:
		template <typename Target> UiInputFrame Run(const Input::InputSystem& input, Target& target, float deltaSeconds);
		std::optional<UiNavDirection> GamepadDirection(const Input::InputSystem& input) const;

		UiInputBridgeDesc desc;
		bool hadFocus = true;
		bool hasPointer = false;
		UiPoint lastPointer;
		std::optional<UiNavDirection> heldDirection;
		float heldSeconds = 0.0f;
		float nextRepeat = 0.0f;
	};
} // namespace Swim::UI
