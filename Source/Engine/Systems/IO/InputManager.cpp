#include "PCH.h"
#include "InputManager.h"
#include "Engine/SwimEngine.h"

namespace Engine
{

	int InputManager::Awake()
	{
		keyCount = 256;
		mouseWheelDelta = 0;
		mousePos = { 0.0f, 0.0f };
		mouseDelta = { 0.0f, 0.0f };

		return 0;
	}

	int InputManager::Init()
	{
		auto engine = SwimEngine::GetInstance();
		if (engine)
		{
			windowHandle = engine->GetWindowHandle();
		}
		else
		{
			throw std::runtime_error("Engine not found for InputManager!");
		}

		for (unsigned int i = 0; i < keyCount; ++i)
		{
			keyState[i] = { static_cast<char>(i), { false, false } };
			deferredState[i] = { static_cast<char>(i), { false, false } };
		}

		return 0;
	}

	void InputManager::Update(double dt)
	{
		// Sync deferredState with keyState
		for (unsigned int i = 0; i < keyCount; ++i)
		{
			keyState[i].second.second = keyState[i].second.first;
			keyState[i].second.first = deferredState[i].second.first;
		}

		// Correct focus check for an embedded child window:
		HWND focus = GetFocus(); // returns the window that has the KB focus in *this* thread
		bool hasFocus = (focus == windowHandle) || (focus && IsChild(windowHandle, focus));

		if (!hasFocus)
		{
			for (auto& key : keyState)
			{
				key.second.first = false;
			}
		}

		// Mouse position stays the same
		POINT mousePoint;
		GetCursorPos(&mousePoint);
		ScreenToClient(windowHandle, &mousePoint);

		mouseDelta = { mousePoint.x - mousePos.x, mousePoint.y - mousePos.y };
		mousePos = { static_cast<float>(mousePoint.x), static_cast<float>(mousePoint.y) };

		mouseWheelDelta = 0;
	}

	void InputManager::InputMessage(UINT uMsg, WPARAM wParam)
	{
		switch (uMsg)
		{
			case WM_KEYDOWN:
				KeySetState((unsigned char)wParam, true);
				if (wParam == VK_SHIFT || wParam == VK_LSHIFT || wParam == VK_RSHIFT ||
					wParam == VK_CONTROL || wParam == VK_LCONTROL || wParam == VK_RCONTROL)
				{
					return; // do not pass these specific key releases to the default window handler
				}
				break;

			case WM_KEYUP:
				KeySetState((unsigned char)wParam, false);
				if (wParam == VK_SHIFT || wParam == VK_LSHIFT || wParam == VK_RSHIFT ||
					wParam == VK_CONTROL || wParam == VK_LCONTROL || wParam == VK_RCONTROL)
				{
					return; // do not pass these specific key releases to the default window handler
				}
				break;

			case WM_SYSKEYDOWN:
				if (wParam == VK_F10)
				{
					KeySetState(VK_F10, true);
				}
				break;

			case WM_SYSKEYUP:
				if (wParam == VK_F10)
				{
					KeySetState(VK_F10, false);
				}
				break;

			case WM_MOUSEWHEEL:
				SetMouseScrollDelta(GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA);
				break;

			case WM_LBUTTONDOWN:
				KeySetState(VK_LBUTTON, true);
				break;

			case WM_LBUTTONUP:
				KeySetState(VK_LBUTTON, false);
				break;

			case WM_RBUTTONDOWN:
				KeySetState(VK_RBUTTON, true);
				break;

			case WM_RBUTTONUP:
				KeySetState(VK_RBUTTON, false);
				break;

			case WM_MBUTTONDOWN:
				KeySetState(VK_MBUTTON, true);
				break;

			case WM_MBUTTONUP:
				KeySetState(VK_MBUTTON, false);
				break;
		}
	}

	// Set the state of the key if it is down or not
	void InputManager::KeySetState(unsigned char key, bool isDown)
	{
		if (key < keyCount)
		{
			deferredState[key].first = key;
			deferredState[key].second.first = isDown;
		}
	}

	// Check if the key is currently down
	bool InputManager::IsKeyDown(unsigned char key) const
	{
		/*
		ImGuiIO& io = ImGui::GetIO();

		if (io.WantCaptureMouse || io.WantCaptureKeyboard)
		{
			return false;
		}
		*/

		if (key <= keyCount && key > 0)
		{
			return keyState[key].second.first;
		}
		return false;
	}

	// Check if the key was just triggered this frame 
	bool InputManager::IsKeyTriggered(unsigned char key) const
	{
		/*
		ImGuiIO& io = ImGui::GetIO();

		if (io.WantCaptureMouse || io.WantCaptureKeyboard)
		{
			return false;
		}
		*/

		if (key <= keyCount && key > 0)
		{
			return keyState[key].second.first && !keyState[key].second.second;
		}
		return false;
	}

	// Check if the key was just released this frame
	bool InputManager::IsKeyReleased(unsigned char key) const
	{
		/*
		ImGuiIO& io = ImGui::GetIO();

		if (io.WantCaptureMouse || io.WantCaptureKeyboard)
		{
			return false;
		}
		*/

		if (key <= keyCount && key > 0)
		{
			return !keyState[key].second.first && keyState[key].second.second;
		}
		return false;
	}

	glm::vec2 InputManager::GetMousePosition(bool adjustForTitleBar, int amount) const
	{
		if (!adjustForTitleBar || amount == 0)
		{
			return mousePos;
		}

		auto engine = Engine::SwimEngine::GetInstance();

		float windowW = static_cast<float>(engine->GetWindowWidth());
		float windowH = static_cast<float>(engine->GetWindowHeight());

		constexpr float virtW = Engine::Renderer::VirtualCanvasWidth;
		constexpr float virtH = Engine::Renderer::VirtualCanvasHeight;

		float scaleX = windowW / virtW;
		float scaleY = windowH / virtH;

		// window top border hack fix (this might differ based on screen resolution)
		if (scaleY > 0.9f)
		{
			amount = 0.f;
		}

		glm::vec2 mouseVirt;
		mouseVirt.x = mousePos.x / scaleX; // undo X scale
		mouseVirt.y = mousePos.y / scaleY; // undo Y scale
		mouseVirt.y = virtH - mouseVirt.y - amount; // flip origin TL -> BL

		return mouseVirt;
	}

}
