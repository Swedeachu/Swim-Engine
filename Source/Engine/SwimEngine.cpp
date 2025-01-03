#include "PCH.h"
#include "SwimEngine.h"
#include <chrono>
#include "Engine/Systems/Renderer/VulkanRenderer.h"

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

		// Show and update the window
		ShowWindow(engineWindowHandle, SW_SHOW);
		UpdateWindow(engineWindowHandle);

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
		*/

		// We must have input system and renderer set up to accept messages
		// I don't remember why we check if our self isn't null, but if we don't then everything breaks
		if (this == nullptr || inputManager.get() == nullptr || renderer.get() == nullptr)
		{
			return DefWindowProc(hwnd, uMsg, wParam, lParam);
		}

		switch (uMsg)
		{
			// closed the window or process from a high user level
			case WM_DESTROY:
			{
				running = false;
				PostQuitMessage(0);
				return 0;
			}

			// dragging the window around
			case WM_MOVE:
			{
				InvalidateRect(engineWindowHandle, NULL, FALSE); // forces a redraw in traditional Microsoft applications, idk about Vulkan
				break;
			}

			// dragging or resizing windows weirdness
			case WM_ENTERSIZEMOVE:
			{
				resizing = true;
				break;
			}

			case WM_SIZE:
			{
				// Always update the window size fields first
				UpdateWindowSize();

				// If minimized, mark it. If restored/maximized, unmark it.
				if (wParam == SIZE_MINIMIZED)
				{
					minimized = true;
				}
				else if (wParam == SIZE_RESTORED || wParam == SIZE_MAXIMIZED)
				{
					minimized = false;
				}

				// If the window dimensions are valid and we are not resizing, flag the need for a resize
				if (windowWidth > 0 && windowHeight > 0 && !resizing)
				{
					needResize = true;
				}

				break;
			}

			case WM_EXITSIZEMOVE:
			{
				// The user just finished dragging the window
				resizing = false;

				// Update the final window dimension after drag
				UpdateWindowSize();

				if (windowWidth > 0 && windowHeight > 0)
				{
					// Mark that we need a resize
					needResize = true;
				}

				break;
			}

			default:
			{
				// Otherwise pass any input to the input manager (we assume any other unhandled message is input)
				inputManager->InputMessage(uMsg, wParam);
			}
		}

		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}

	int SwimEngine::Init()
	{
		// Add systems to the SystemManager
		inputManager = systemManager->AddSystem<InputManager>("InputManager");
		sceneSystem = systemManager->AddSystem<SceneSystem>("SceneSystem");
		renderer = systemManager->AddSystem<VulkanRenderer>("Renderer", engineWindowHandle, windowWidth, windowHeight);
		cameraSystem = systemManager->AddSystem<CameraSystem>("CameraSystem");

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
		unsigned int tickCounter = 1;          // Start tick counter at 1

		// Maximum allowable delta time (e.g., 5x the fixed time step)
		const double maxDeltaTime = 5.0 * fixedTimeStep;

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

			// Safety check: If delta time exceeds maxDeltaTime, log and skip the frame
			// This is most often caused when dragging around the window or doing something of that nature to suspend the process temporarily
			// Detatched multi threading would make that not happen for most cases though.
			if (delta > maxDeltaTime)
			{
				std::cerr << "Frame skipped due to excessive delta time: " << delta << " seconds.\n";
				accumulatedTime = 0.0; // Reset accumulated time to avoid cascading lag effects
				continue;
			}

			accumulatedTime += delta;

			// Perform fixed updates as needed
			while (accumulatedTime >= fixedTimeStep)
			{
				FixedUpdate(tickCounter); // Pass the current tick index
				accumulatedTime -= fixedTimeStep;

				// Increment the tick counter, resetting to 1 after tickRate
				tickCounter++;
				if (tickCounter > tickRate)
				{
					tickCounter = 1; // Reset to 1 for the next second
				}
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
		// First sync any updates that happened to the window to the renderer
		// But we only do this if we are not a minimized window
		if (!minimized && needResize && renderer)
		{
			renderer->SetFramebufferResized();
			needResize = false;
		}

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
		renderer->SetSurfaceSize(windowHeight, windowHeight);
	}

}
