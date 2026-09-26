#include "Engine/Systems/UiInput/UiInputBridge.h"
#include "Engine/Systems/Text/Utf8.h"

#include <cmath>
#include <optional>
#include <stdexcept>
#include <type_traits>

namespace Swim::UI
{
	namespace
	{
		std::optional<UiKey> ToUiKey(Platform::KeyCode key)
		{
			using K = Platform::KeyCode;
			switch (key)
			{
			case K::Left:
				return UiKey::Left;
			case K::Right:
				return UiKey::Right;
			case K::Up:
				return UiKey::Up;
			case K::Down:
				return UiKey::Down;
			case K::Home:
				return UiKey::Home;
			case K::End:
				return UiKey::End;
			case K::PageUp:
				return UiKey::PageUp;
			case K::PageDown:
				return UiKey::PageDown;
			case K::Backspace:
				return UiKey::Backspace;
			case K::Delete:
				return UiKey::Delete;
			case K::Enter:
				return UiKey::Enter;
			case K::Space:
				return UiKey::Space;
			case K::Tab:
				return UiKey::Tab;
			case K::Escape:
				return UiKey::Escape;
			case K::A:
				return UiKey::A;
			case K::C:
				return UiKey::C;
			case K::V:
				return UiKey::V;
			case K::X:
				return UiKey::X;
			default:
				return std::nullopt;
			}
		}

		UiKey ArrowKey(UiNavDirection direction)
		{
			switch (direction)
			{
			case UiNavDirection::Up:
				return UiKey::Up;
			case UiNavDirection::Down:
				return UiKey::Down;
			case UiNavDirection::Left:
				return UiKey::Left;
			default:
				return UiKey::Right;
			}
		}

		// SDL reports the composition cursor in code points; UiDocument takes bytes.
		std::uint32_t CodePointsToBytes(const std::string& text, int codePoints)
		{
			std::size_t offset = 0;
			for (int i = 0; i < codePoints && offset < text.size(); ++i)
			{
				offset += Text::DecodeUtf8(text, offset).Length;
			}
			return static_cast<std::uint32_t>(std::min(offset, text.size()));
		}

		struct DocumentTarget
		{
			UiDocument& Document;
			UiPoint Last;

			void Move(UiPoint point)
			{
				Document.PointerMove(point); // Lays the document out again if it changed.
				Last = point;
			}

			bool Over() const { return Document.IsLayoutCurrent() && static_cast<bool>(Document.HitTest(Last)); }

			void Down(UiKeyModifiers modifiers) { Document.PointerDown(Last, modifiers); }

			void Up() { Document.PointerUp(Last); }

			bool Wheel(UiPoint delta) { return Document.Wheel(Last, delta); }

			void LoseFocus()
			{
				Document.CancelPointer();
				Document.Focus({});
			}

			bool ContextMenu() { return Document.OpenContextMenu(Last); }

			bool ContextMenuForFocus() { return Document.OpenContextMenuForFocus(); }

			bool HasFocus() const { return static_cast<bool>(Document.GetFocus()); }

			bool FocusedEditable() const { return Document.GetFocus() && Document.IsEditable(Document.GetFocus()); }

			bool KeyDown(UiKey key, UiKeyModifiers modifiers) { return Document.KeyDown(key, modifiers); }

			bool Navigate(UiNavDirection direction) { return Document.Navigate(direction); }

			void TextInput(std::string_view text) { Document.TextInput(text); }

			void Composition(std::string_view text, std::uint32_t cursor) { Document.SetComposition(text, cursor); }

			bool WantsTextInput() const { return Document.WantsTextInput(); }
		};

		struct RouterTarget
		{
			UiCanvasRouter& Router;
			const UiCameraView* Camera;
			std::span<const UiSurfaceHit> Hits;

			void Move(UiPoint point)
			{
				UiPointer pointer;
				pointer.Screen = point;
				if (Camera)
				{
					pointer.Ray = ScreenRay(*Camera, point);
				}
				pointer.SurfaceHits = Hits;
				Router.PointerMove(pointer);
			}

			bool Over() const { return Router.IsPointerOverUi(); }

			void Down(UiKeyModifiers modifiers) { Router.PointerDown(modifiers); }

			void Up() { Router.PointerUp(); }

			bool Wheel(UiPoint delta) { return Router.Wheel(delta); }

			void LoseFocus()
			{
				Router.CancelPointer();
				Router.ClearFocus();
			}

			bool ContextMenu() { return Router.OpenContextMenu(); }

			bool ContextMenuForFocus() { return Router.OpenContextMenuForFocus(); }

			UiDocument* Focused() const { return Router.GetDocument(Router.GetFocused()); }

			bool HasFocus() const { return Focused() && Focused()->GetFocus(); }

			bool FocusedEditable() const { return HasFocus() && Focused()->IsEditable(Focused()->GetFocus()); }

			bool KeyDown(UiKey key, UiKeyModifiers modifiers) { return Router.KeyDown(key, modifiers); }

			bool Navigate(UiNavDirection direction) { return Router.Navigate(direction); }

			void TextInput(std::string_view text) { Router.TextInput(text); }

			void Composition(std::string_view text, std::uint32_t cursor) { Router.SetComposition(text, cursor); }

			bool WantsTextInput() const { return Router.WantsTextInput(); }
		};
	} // namespace

	UiInputBridge::UiInputBridge(UiInputBridgeDesc descInput) : desc(descInput)
	{
		if (!std::isfinite(desc.FramebufferScale) || desc.FramebufferScale <= 0.0f || !std::isfinite(desc.WheelStep) ||
			desc.WheelStep <= 0.0f || !std::isfinite(desc.StickThreshold) || desc.StickThreshold <= 0.0f || desc.StickThreshold > 1.0f ||
			!std::isfinite(desc.RepeatDelaySeconds) || desc.RepeatDelaySeconds <= 0.0f || !std::isfinite(desc.RepeatIntervalSeconds) ||
			desc.RepeatIntervalSeconds <= 0.0f)
		{
			throw std::invalid_argument(
				"UI input bridge needs a positive framebuffer scale, wheel step, stick threshold and repeat timing");
		}
	}

	std::optional<UiNavDirection> UiInputBridge::GamepadDirection(const Input::InputSystem& input) const
	{
		if (!desc.Gamepad || !input.IsGamepadConnected(*desc.Gamepad))
		{
			return std::nullopt;
		}
		const auto device = *desc.Gamepad;
		using B = Platform::GamepadButton;
		if (input.IsGamepadButtonDown(device, B::DpadUp))
		{
			return UiNavDirection::Up;
		}
		if (input.IsGamepadButtonDown(device, B::DpadDown))
		{
			return UiNavDirection::Down;
		}
		if (input.IsGamepadButtonDown(device, B::DpadLeft))
		{
			return UiNavDirection::Left;
		}
		if (input.IsGamepadButtonDown(device, B::DpadRight))
		{
			return UiNavDirection::Right;
		}
		const float x = input.GetGamepadAxis(device, Platform::GamepadAxis::LeftX);
		const float y = input.GetGamepadAxis(device, Platform::GamepadAxis::LeftY); // Positive down (SDL).
		if (std::max(std::abs(x), std::abs(y)) < desc.StickThreshold)
		{
			return std::nullopt;
		}
		if (std::abs(x) >= std::abs(y))
		{
			return x < 0.0f ? UiNavDirection::Left : UiNavDirection::Right;
		}
		return y < 0.0f ? UiNavDirection::Up : UiNavDirection::Down;
	}

	template <typename Target> UiInputFrame UiInputBridge::Run(const Input::InputSystem& input, Target& target, float deltaSeconds)
	{
		if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0f)
		{
			throw std::invalid_argument("UI input bridge needs a non-negative frame time");
		}
		UiInputFrame frame;
		if (!input.HasFocus())
		{
			if (hadFocus)
			{
				target.LoseFocus();
			}
			hadFocus = false;
			hasPointer = false;
			heldDirection.reset();
			return frame;
		}
		hadFocus = true;

		UiKeyModifiers modifiers;
		modifiers.Shift = input.IsShiftDown();
		modifiers.Alt = input.IsAltDown();
		modifiers.Control = desc.Shortcut == UiShortcutModifier::Super
			? input.IsKeyDown(Platform::KeyCode::LeftSuper) || input.IsKeyDown(Platform::KeyCode::RightSuper)
			: input.IsControlDown();

		const auto mouse = input.GetMousePosition();
		const UiPoint pointer{ mouse.X * desc.FramebufferScale, mouse.Y * desc.FramebufferScale };
		constexpr bool alwaysMove = std::is_same_v<Target, RouterTarget>; // World canvases move under a still mouse.
		if (alwaysMove || !hasPointer || pointer.X != lastPointer.X || pointer.Y != lastPointer.Y)
		{
			target.Move(pointer);
			lastPointer = pointer;
			hasPointer = true;
		}
		frame.PointerOverUi = target.Over();
		if (input.IsMouseButtonTriggered(Platform::MouseButton::Left))
		{
			target.Down(modifiers);
		}
		if (input.IsMouseButtonReleased(Platform::MouseButton::Left))
		{
			target.Up();
		}
		if (input.IsMouseButtonTriggered(Platform::MouseButton::Right) && target.ContextMenu())
		{
			frame.ContextMenuOpened = true;
		}
		const float wheel = input.GetMouseScrollDelta();
		if (wheel != 0.0f && std::isfinite(wheel))
		{
			// A positive wheel delta scrolls up, towards smaller offsets.
			target.Wheel({ 0.0f, -wheel * desc.WheelStep });
		}

		for (const auto& event : input.GetTextEditEvents())
		{
			if (!event.Text.empty())
			{
				target.TextInput(event.Text);
				continue;
			}
			if (event.Key == Platform::KeyCode::F10 && modifiers.Shift && target.HasFocus())
			{
				// Shift+F10: the focused node's context menu.
				if (target.ContextMenuForFocus())
				{
					frame.ContextMenuOpened = true;
					++frame.KeysConsumed;
				}
				continue;
			}
			const auto mapped = ToUiKey(event.Key);
			if (!mapped)
			{
				continue;
			}
			if (!target.HasFocus() && !(*mapped == UiKey::Tab && desc.TabStartsNavigation))
			{
				continue;
			}
			if (target.KeyDown(*mapped, modifiers))
			{
				++frame.KeysConsumed;
			}
		}
		if (input.HasTextCompositionUpdate())
		{
			const auto& composition = input.GetTextComposition();
			const int start = input.GetTextCompositionStart();
			target.Composition(
				composition, start < 0 ? static_cast<std::uint32_t>(composition.size()) : CodePointsToBytes(composition, start));
		}

		// Gamepad navigation.
		if (desc.Gamepad && input.IsGamepadConnected(*desc.Gamepad))
		{
			using B = Platform::GamepadButton;
			const auto device = *desc.Gamepad;
			const auto direction = GamepadDirection(input);
			bool fire = false;
			if (direction != heldDirection)
			{
				heldDirection = direction;
				heldSeconds = 0.0f;
				nextRepeat = desc.RepeatDelaySeconds;
				fire = direction.has_value();
			}
			else if (direction)
			{
				heldSeconds += deltaSeconds;
				if (heldSeconds >= nextRepeat)
				{
					fire = true;
					nextRepeat = heldSeconds + desc.RepeatIntervalSeconds;
				}
			}
			if (fire)
			{
				// A text field would keep arrows for its caret; the pad always leaves it.
				const bool consumed =
					target.HasFocus() && !target.FocusedEditable() ? target.KeyDown(ArrowKey(*direction), {}) : target.Navigate(*direction);
				frame.KeysConsumed += consumed ? 1u : 0u;
			}
			if (target.HasFocus())
			{
				if (input.IsGamepadButtonTriggered(device, B::South))
				{
					frame.KeysConsumed += target.KeyDown(UiKey::Enter, {}) ? 1u : 0u;
				}
				if (input.IsGamepadButtonTriggered(device, B::East))
				{
					frame.KeysConsumed += target.KeyDown(UiKey::Escape, {}) ? 1u : 0u;
				}
				if (input.IsGamepadButtonTriggered(device, B::North) && target.ContextMenuForFocus())
				{
					frame.ContextMenuOpened = true;
					++frame.KeysConsumed;
				}
			}
			if (input.IsGamepadButtonTriggered(device, B::LeftShoulder) || input.IsGamepadButtonTriggered(device, B::RightShoulder))
			{
				UiKeyModifiers shift;
				shift.Shift = input.IsGamepadButtonTriggered(device, B::LeftShoulder);
				frame.KeysConsumed += target.KeyDown(UiKey::Tab, shift) ? 1u : 0u;
			}
			frame.GamepadCaptured = target.HasFocus();
		}
		frame.KeyboardCaptured = target.HasFocus();
		frame.WantsTextInput = target.WantsTextInput();
		return frame;
	}

	UiInputFrame UiInputBridge::Apply(const Input::InputSystem& input, UiDocument& document, float deltaSeconds)
	{
		DocumentTarget target{ document, lastPointer };
		return Run(input, target, deltaSeconds);
	}

	UiInputFrame UiInputBridge::Apply(const Input::InputSystem& input, UiCanvasRouter& router, const UiCameraView* camera,
		std::span<const UiSurfaceHit> surfaceHits, float deltaSeconds)
	{
		RouterTarget target{ router, camera, surfaceHits };
		return Run(input, target, deltaSeconds);
	}
} // namespace Swim::UI
