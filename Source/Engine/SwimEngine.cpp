#include "PCH.h"
#include "SwimEngine.h"
#include <chrono>
#include "Systems/Scene/SceneSystem.h"

namespace Engine
{

	// Global engine instance
	std::shared_ptr<SwimEngine> EngineInstance = nullptr;

	SwimEngine::SwimEngine()
	{
		windowClassName = L"SwimEngine";
		windowTitle = L"Demo";
		systemManager = std::make_unique<SystemManager>();
	}

	std::shared_ptr<SwimEngine> SwimEngine::GetInstance()
	{
		return EngineInstance;
	}

	bool SwimEngine::Start()
	{
		// Ensure this is being managed by a shared_ptr
		if (auto self = shared_from_this(); self)
		{
			EngineInstance = self;
		}
		else
		{
			throw std::runtime_error("SwimEngine must be managed by a shared_ptr.");
		}

		if (Awake() == 0) return Init(); // init if zero errors happened

		return false;
	}

	int SwimEngine::Awake()
	{
		if (!MakeWindow()) return -1;

		return 0;
	}

	bool SwimEngine::MakeWindow()
	{
		hInstance = GetModuleHandle(nullptr);

		// Construct and register window class
		WNDCLASSEX wc = {};
		wc.cbSize = sizeof(WNDCLASSEX);
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = StaticWindowProc;
		wc.hInstance = hInstance;
		wc.hCursor = LoadCursor(0, IDC_ARROW);
		wc.lpszClassName = windowClassName.c_str();

		RegisterClassEx(&wc);

		// Create window handle
		engineWindowHandle = CreateWindowEx(
			0, // window style dword enum (no idea what this is for, I assume for setting if the window has minimize and expand options?)
			windowClassName.c_str(), // class name
			windowTitle.c_str(), // window title
			WS_OVERLAPPEDWINDOW, // window style (will be important for full screen and tabbed)
			CW_USEDEFAULT, // initial horizontal position of the window
			CW_USEDEFAULT, // initial vertical position of the window
			windowWidth, // width
			windowHeight, // height
			nullptr, // window parent (none)
			nullptr, // window child (none)
			hInstance, // window instance handle
			this // Set the GWLP_USERDATA to the Engine instance
		);

		// error if the window isn't made correctly
		if (engineWindowHandle == nullptr || !&wc)
		{
			return false;
		}

		return true;
	}

	LRESULT CALLBACK SwimEngine::StaticWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		// Retrieve the pointer to the Engine instance
		SwimEngine* enginePtr = reinterpret_cast<SwimEngine*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

		if (uMsg == WM_CREATE)
		{
			// Set the GWLP_USERDATA value to the SwimEngine instance when the window is created
			CREATESTRUCT* createStruct = reinterpret_cast<CREATESTRUCT*>(lParam);
			enginePtr = reinterpret_cast<SwimEngine*>(createStruct->lpCreateParams);
			SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(enginePtr));
		}

		// Call the non-static member function for window procedure
		return enginePtr->WindowProc(hwnd, uMsg, wParam, lParam);
	}

	LRESULT CALLBACK SwimEngine::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		/*
		if (ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam))
		{
			return true;
		}

		// we must have input system and renderer set up to accept messages
		if (this == nullptr || inputSystem == nullptr || renderer == nullptr)
		{
			return DefWindowProc(hwnd, uMsg, wParam, lParam);;
		}
		*/

		switch (uMsg)
		{
			// destroy handling ---------------------------------------------------------------
			case WM_DESTROY:
				// running = false; // make sure to tell that we are not running anymore
				PostQuitMessage(0);
				return 0;
				// move handling ----------------------------------------------------------------
			case WM_MOVE:
				InvalidateRect(engineWindowHandle, NULL, FALSE); // force a redraw
				break;
				break;
				// size handling ----------------------------------------------------------------
			case WM_SIZE:
				// reset renderer window size on size changing
				if (wParam == SIZE_MAXIMIZED)
				{
					//	renderer->GetCamera()->resetWindowSize();
				}
				if (wParam == SIZE_RESTORED && !resizing)
				{
					//	renderer->GetCamera()->resetWindowSize();
				}
				UpdateWindowSize();
				break;
			case WM_EXITSIZEMOVE:
				if (resizing)
				{
					UpdateWindowSize();
					// renderer->GetCamera()->resetWindowSize();
				}
				resizing = false;
				break;
				// Input handling from here ------------------------------------------------------
				/*
			case WM_KEYDOWN:
				inputSystem->keySetState((unsigned char)wParam, true);
				if (wParam == VK_SHIFT || wParam == VK_LSHIFT || wParam == VK_RSHIFT ||
					wParam == VK_CONTROL || wParam == VK_LCONTROL || wParam == VK_RCONTROL)
				{
					return 0; // do not pass these specific key releases to the default window handler
				}
				break;
			case WM_KEYUP:
				inputSystem->keySetState((unsigned char)wParam, false);
				if (wParam == VK_SHIFT || wParam == VK_LSHIFT || wParam == VK_RSHIFT ||
					wParam == VK_CONTROL || wParam == VK_LCONTROL || wParam == VK_RCONTROL)
				{
					return 0; // do not pass these specific key releases to the default window handler
				}
				break;
			case WM_SYSKEYDOWN:
				if (wParam == VK_F10)
				{
					inputSystem->keySetState(VK_F10, true);
				}
				break;
			case WM_SYSKEYUP:
				if (wParam == VK_F10)
				{
					inputSystem->keySetState(VK_F10, false);
				}
				break;
			case WM_MOUSEWHEEL:
				inputSystem->setMouseScrollDelta(GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA);
				break;
			case WM_LBUTTONDOWN:
				inputSystem->keySetState(VK_LBUTTON, true);
				break;
			case WM_LBUTTONUP:
				inputSystem->keySetState(VK_LBUTTON, false);
				break;
			case WM_RBUTTONDOWN:
				inputSystem->keySetState(VK_RBUTTON, true);
				break;
			case WM_RBUTTONUP:
				inputSystem->keySetState(VK_RBUTTON, false);
				break;
			case WM_MBUTTONDOWN:
				inputSystem->keySetState(VK_MBUTTON, true);
				break;
			case WM_MBUTTONUP:
				inputSystem->keySetState(VK_MBUTTON, false);
				break;
				*/
		}

		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}

	int SwimEngine::Init()
	{
		// Add systems to the SystemManager
		systemManager->AddSystem<SceneSystem>("SceneSystem");

		// Call Awake and Init on all systems
		if (systemManager->Awake() != 0)
		{
			return -1; // Error during system initialization
		}

		if (systemManager->Init() != 0)
		{
			return -1; // Error during system initialization
		}

		return 0;
	}

	int SwimEngine::Run()
	{
		// Show and update the window
		ShowWindow(engineWindowHandle, SW_SHOW);
		UpdateWindow(engineWindowHandle);

		return HeartBeat();
	}

	void SwimEngine::Stop()
	{
		running = false;
	}

	int SwimEngine::HeartBeat()
	{
		MSG msg = {};
		running = true;

		// Timing variables
		auto previousTime = std::chrono::high_resolution_clock::now();
		double accumulatedTime = 0.0;
		double fixedTimeStep = 1.0 / tickRate; // e.g., 20 ticks per second

		while (running)
		{
			// Handle window messages
			while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);

				if (msg.message == WM_QUIT)
				{
					running = false;
					break;
				}
			}

			// Calculate delta time
			auto currentTime = std::chrono::high_resolution_clock::now();
			std::chrono::duration<double> elapsed = currentTime - previousTime;
			previousTime = currentTime;

			double delta = elapsed.count();
			accumulatedTime += delta;

			// Perform fixed updates as needed
			while (accumulatedTime >= fixedTimeStep)
			{
				FixedUpdate(totalFrames % tickRate); // Pass the tick index within the second
				accumulatedTime -= fixedTimeStep;
			}

			// Perform frame updates
			Update(delta);

			// Frame counting
			++totalFrames;
		}

		return Exit();
	}

	void SwimEngine::Update(double dt)
	{
		systemManager->Update(dt);
	}

	void SwimEngine::FixedUpdate(unsigned int tickThisSecond)
	{
		systemManager->FixedUpdate(tickThisSecond);
	}

	int SwimEngine::Exit()
	{
		return systemManager->Exit();
	}

	void SwimEngine::UpdateWindowSize()
	{
		RECT rect;
		GetClientRect(engineWindowHandle, &rect);
		windowWidth = rect.right - rect.left;
		windowHeight = rect.bottom - rect.top;
	}

}
