#pragma once

#include "InputTypes.h"
#include "Window.h"
#include "WindowEvents.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Swim::Platform
{

	// One entry of a file dialog's type list: Pattern is extensions separated by ';' ("png;jpg")
	// or "*" for any file.
	struct FileDialogFilter
	{
		std::string Name;
		std::string Pattern;
	};

	enum class UserFolder : std::uint8_t
	{
		Home,
		Desktop,
		Documents,
		Downloads,
		Pictures,
	};

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

		using FileDialogCallback = std::function<void(std::optional<std::filesystem::path> chosen)>;

		// Opens the system's open-file dialog without blocking. The callback runs once, on the
		// thread that pumps events, with the chosen file or nothing when cancelled or failed.
		// start is the folder (or file) the dialog first shows; parent may be null.
		void ShowOpenFileDialog(Window* parent, std::vector<FileDialogFilter> filters, const std::filesystem::path& start,
			FileDialogCallback callback);

		// The user's folder of that kind; empty when the platform has none.
		static std::filesystem::path GetUserFolder(UserFolder folder);

		// The system clipboard as UTF-8 text; empty when it holds no text or there is no display.
		static std::string GetClipboardText();

		static void SetClipboardText(std::string_view text);

	private:

		struct Impl;
		std::unique_ptr<Impl> impl;

	};

}
