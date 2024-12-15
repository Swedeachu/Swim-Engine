#pragma once

#include "Systems/SystemManager.h"

namespace Engine
{

	class VulkanRenderer; // forward decalre

	// std::enable_shared_from_this<SwimEngine> so we can get a pointer to ourselves
	class SwimEngine : public Machine, public std::enable_shared_from_this<SwimEngine>
	{

	public:

		SwimEngine();

		// Calls Awake and then Init
		bool Start();

		// Creates the window and anything else needed to be made first
		int Awake() override;

		// Sets up things once the window is made (Vulkan + other Core systems)
		int Init() override;

		// triggers the main loop
		int Run();

		// Called when it is time
		void Update(double dt) override;

		// Called at a fixed rate N times a second (20 by default)
		void FixedUpdate(unsigned int tickThisSecond) override;

		// Called when the engine is closed (release file locks, write final logs, etc)
		int Exit() override;

		// Makes the engine break out of the heart beat loop if it is in it, and calls Exit()
		void Stop();

		static std::shared_ptr<SwimEngine> GetInstance();

		HWND GetWindowHandle() const { return engineWindowHandle; }

		std::shared_ptr<InputManager> GetInputManager() const { return inputManager; }
		std::shared_ptr<SceneSystem> GetSceneSystem() const { return sceneSystem; }
		std::shared_ptr<CameraSystem> GetCameraSystem() const { return cameraSystem; }
		std::shared_ptr<VulkanRenderer> GetRenderer() const { return renderer; }

		unsigned int GetWindowWidth() const { return windowWidth; }
		unsigned int GetWindowHeight() const { return windowHeight; }

	private:

		// calls Update when it is time
		int HeartBeat();

		bool MakeWindow();

		// Engine controls
		bool uncappedFPS{ true };
		unsigned int targetFPS{ 60 };
		unsigned int totalFrames{ 0 };
		unsigned int tickRate{ 20 };
		double frameTime{ 0.0 };
		double delta{ 0.0 };
		bool running{ false };
		bool resizing{ false };
		bool fullscreen{ false };
		bool cursorVisible{ true };
		bool debugging{ false };

		// Window fields
		HWND engineWindowHandle{ nullptr };
		HINSTANCE hInstance{ nullptr };
		unsigned int windowWidth{ 1280 };
		unsigned int windowHeight{ 720 };
		std::wstring windowTitle{ L"Demo" };
		std::wstring windowClassName{ L"SwimEngine" };

		// Window message handler
		static LRESULT CALLBACK StaticWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
		LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

		// Private Window Helpers
		void UpdateWindowSize();

		std::unique_ptr<SystemManager> systemManager;
		std::shared_ptr<InputManager> inputManager;
		std::shared_ptr<SceneSystem> sceneSystem;
		std::shared_ptr<VulkanRenderer> renderer;
		std::shared_ptr<CameraSystem> cameraSystem;

	};

}