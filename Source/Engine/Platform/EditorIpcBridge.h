#pragma once

#include "Window.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

namespace Swim::Platform
{

	class EditorIpcBridge
	{
	public:

		using MessageHandler = std::function<void(std::string_view)>;

		EditorIpcBridge();
		EditorIpcBridge(const EditorIpcBridge&) = delete;
		EditorIpcBridge& operator=(const EditorIpcBridge&) = delete;
		~EditorIpcBridge();

		bool Initialize(Window& window, NativeWindowHandle editorWindow, MessageHandler handler);
		void Shutdown();
		bool Send(std::string_view message, std::uintptr_t channel = 1) const;
		bool IsAvailable() const;

	private:

		struct Impl;
		std::unique_ptr<Impl> impl;
	};

}
