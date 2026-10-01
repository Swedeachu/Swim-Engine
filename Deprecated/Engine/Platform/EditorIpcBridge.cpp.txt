#include "EditorIpcBridge.h"
#include <utility>

#if defined(_WIN32)
	#include <SDL3/SDL_system.h>
	#include "Internal/WindowsApi.h"
	#include <string>
#endif

namespace Swim::Platform
{

#if defined(_WIN32)
	namespace
	{

		std::string WideToUtf8(std::wstring_view value)
		{
			if (value.empty())
			{
				return {};
			}

			const int required = WideCharToMultiByte(
				CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr
			);
			if (required <= 0)
			{
				return {};
			}

			std::string result(static_cast<size_t>(required), '\0');
			WideCharToMultiByte(
				CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), required, nullptr, nullptr
			);
			return result;
		}

		std::wstring Utf8ToWide(std::string_view value)
		{
			if (value.empty())
			{
				return {};
			}

			const int required = MultiByteToWideChar(
				CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0
			);
			if (required <= 0)
			{
				return {};
			}

			std::wstring result(static_cast<size_t>(required), L'\0');
			MultiByteToWideChar(
				CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), required
			);
			return result;
		}

	}
#endif

	struct EditorIpcBridge::Impl
	{
		NativeWindowHandle EngineWindow{};
		NativeWindowHandle EditorWindow{};
		MessageHandler Handler;
		bool Initialized = false;

#if defined(_WIN32)
		static bool SDLCALL WindowsMessageHook(void* userdata, MSG* message)
		{
			auto* impl = static_cast<Impl*>(userdata);
			if (!impl || !impl->Initialized || !message)
			{
				return true;
			}

			if (message->hwnd != static_cast<HWND>(impl->EngineWindow.Window) || message->message != WM_COPYDATA)
			{
				return true;
			}

			auto* copyData = reinterpret_cast<COPYDATASTRUCT*>(message->lParam);
			if (!copyData || !copyData->lpData || copyData->cbData < sizeof(wchar_t))
			{
				return false;
			}

			const wchar_t* text = static_cast<const wchar_t*>(copyData->lpData);
			size_t count = static_cast<size_t>(copyData->cbData / sizeof(wchar_t));
			if (count > 0 && text[count - 1] == L'\0')
			{
				--count;
			}

			if (impl->Handler)
			{
				std::string utf8 = WideToUtf8(std::wstring_view(text, count));
				impl->Handler(utf8);
			}
			return false;
		}
#endif
	};


	EditorIpcBridge::EditorIpcBridge() : impl(std::make_unique<Impl>()) {}

	EditorIpcBridge::~EditorIpcBridge()
	{
		Shutdown();
	}

	bool EditorIpcBridge::Initialize(Window& window, NativeWindowHandle editorWindow, MessageHandler handler)
	{
		Shutdown();
		if (!impl)
		{
			impl = std::make_unique<Impl>();
		}

		impl->EngineWindow = window.GetNativeHandle();
		impl->EditorWindow = editorWindow;
		impl->Handler = std::move(handler);

#if defined(_WIN32)
		if (impl->EngineWindow.Type != NativeWindowType::Win32 || impl->EditorWindow.Type != NativeWindowType::Win32 ||
			!impl->EngineWindow.Window || !impl->EditorWindow.Window)
		{
			return false;
		}

		impl->Initialized = true;
		SDL_SetWindowsMessageHook(Impl::WindowsMessageHook, impl.get());
		return true;
#else
		return false;
#endif
	}

	void EditorIpcBridge::Shutdown()
	{
		if (!impl || !impl->Initialized)
		{
			return;
		}

#if defined(_WIN32)
		SDL_SetWindowsMessageHook(nullptr, nullptr);
#endif
		impl->Initialized = false;
		impl->Handler = {};
		impl->EngineWindow = {};
		impl->EditorWindow = {};
	}

	bool EditorIpcBridge::Send(std::string_view message, std::uintptr_t channel) const
	{
#if defined(_WIN32)
		if (!IsAvailable())
		{
			return false;
		}

		std::wstring wide = Utf8ToWide(message);
		COPYDATASTRUCT copyData{};
		copyData.dwData = static_cast<ULONG_PTR>(channel);
		copyData.cbData = static_cast<DWORD>((wide.size() + 1) * sizeof(wchar_t));
		copyData.lpData = wide.data();

		LRESULT result = SendMessageW(
			static_cast<HWND>(impl->EditorWindow.Window),
			WM_COPYDATA,
			reinterpret_cast<WPARAM>(impl->EngineWindow.Window),
			reinterpret_cast<LPARAM>(&copyData)
		);
		return result != 0;
#else
		(void)message;
		(void)channel;
		return false;
#endif
	}

	bool EditorIpcBridge::IsAvailable() const
	{
		return impl && impl->Initialized;
	}

}
