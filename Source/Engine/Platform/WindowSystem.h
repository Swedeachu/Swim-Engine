#pragma once

#include "InputTypes.h"
#include "Window.h"
#include "WindowEvents.h"
#include <functional>
#include <memory>
#include <vector>

namespace Swim::Platform
{

	class WindowSystem
	{
	public:

		using WindowEventHandler = std::function<void(const WindowEvent&)>;
		using InputEventHandler = std::function<void(const InputEvent&)>;

		WindowSystem();
		~WindowSystem();

		std::unique_ptr<Window> Create(const WindowDesc& desc);
		void PumpEvents(const WindowEventHandler& windowHandler, const InputEventHandler& inputHandler);
		std::vector<DisplayInfo> GetDisplays() const;

		void StartTextInput(Window& window);
		void StopTextInput(Window& window);
		// Where the IME candidate/composition window should appear, in window coordinates
		// (the focused text caret; UiDocument::GetTextInputRect divided by the pixel scale).
		// cursor is the caret's horizontal offset inside the rectangle.
		void SetTextInputArea(Window& window, int x, int y, int width, int height, int cursor = 0);
		bool SetGamepadRumble(InputDeviceId device, float lowFrequency, float highFrequency, uint32_t durationMilliseconds);

	private:

		struct Impl;
		std::unique_ptr<Impl> impl;
	};

}
