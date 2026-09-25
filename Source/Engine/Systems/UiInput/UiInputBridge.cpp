#include "Engine/Systems/UiInput/UiInputBridge.h"
#include "Engine/Systems/Text/Utf8.h"

#include <cmath>
#include <optional>
#include <stdexcept>

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
	} // namespace

	UiInputBridge::UiInputBridge(UiInputBridgeDesc descInput) : desc(descInput)
	{
		if (!std::isfinite(desc.FramebufferScale) || desc.FramebufferScale <= 0.0f || !std::isfinite(desc.WheelStep) ||
			desc.WheelStep <= 0.0f)
		{
			throw std::invalid_argument("UI input bridge needs a positive framebuffer scale and wheel step");
		}
	}

	UiInputFrame UiInputBridge::Apply(const Input::InputSystem& input, UiDocument& document)
	{
		UiInputFrame frame;
		if (!input.HasFocus())
		{
			if (hadFocus)
			{
				document.CancelPointer();
				document.Focus({});
			}
			hadFocus = false;
			hasPointer = false;
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
		if (!hasPointer || pointer.X != lastPointer.X || pointer.Y != lastPointer.Y)
		{
			document.PointerMove(pointer); // Lays the document out again if it changed.
			lastPointer = pointer;
			hasPointer = true;
		}
		frame.PointerOverUi = document.IsLayoutCurrent() && static_cast<bool>(document.HitTest(pointer));
		if (input.IsMouseButtonTriggered(Platform::MouseButton::Left))
		{
			document.PointerDown(pointer, modifiers);
		}
		if (input.IsMouseButtonReleased(Platform::MouseButton::Left))
		{
			document.PointerUp(pointer);
		}
		const float wheel = input.GetMouseScrollDelta();
		if (wheel != 0.0f && std::isfinite(wheel))
		{
			// A positive wheel delta scrolls up, towards smaller offsets.
			document.Wheel(pointer, { 0.0f, -wheel * desc.WheelStep });
		}

		for (const auto& event : input.GetTextEditEvents())
		{
			if (!event.Text.empty())
			{
				document.TextInput(event.Text);
				continue;
			}
			const auto mapped = ToUiKey(event.Key);
			if (!mapped)
			{
				continue;
			}
			const bool focused = static_cast<bool>(document.GetFocus());
			if (!focused && !(*mapped == UiKey::Tab && desc.TabStartsNavigation))
			{
				continue;
			}
			if (document.KeyDown(*mapped, modifiers))
			{
				++frame.KeysConsumed;
			}
		}
		if (input.HasTextCompositionUpdate())
		{
			const auto& composition = input.GetTextComposition();
			const int start = input.GetTextCompositionStart();
			document.SetComposition(
				composition, start < 0 ? static_cast<std::uint32_t>(composition.size()) : CodePointsToBytes(composition, start));
		}
		frame.KeyboardCaptured = static_cast<bool>(document.GetFocus());
		frame.WantsTextInput = document.WantsTextInput();
		return frame;
	}
} // namespace Swim::UI
