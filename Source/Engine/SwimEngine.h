#pragma once

#include "Systems/SystemManager.h"

namespace Engine
{

	// forward declare
	class VulkanRenderer;
	class OpenGLRenderer;

	// std::enable_shared_from_this<SwimEngine> so we can get a pointer to ourselves
	class SwimEngine : public Machine, public std::enable_shared_from_this<SwimEngine>
	{

	public:

		enum RenderContext
		{
			Vulkan, OpenGL /*, DirectX11, DirectX12, Metal */ // everything commented out is not implemented yet/doesn't need to be implemented
		};

		// The render context we are using, this should be changed before compliation before building for the target platform.
		// In a fancier world this value would be auto changed via build script steps when doing a batch build for all platforms.
		// This is constexpr so branching logic based on API specific code is instant (Mesh, Texture, etc). 
		static constexpr RenderContext CONTEXT = RenderContext::Vulkan;

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
		static std::shared_ptr<SwimEngine>& GetInstanceRef();

		static std::string GetExecutableDirectory();

		HWND GetWindowHandle() const { return engineWindowHandle; }

		std::shared_ptr<InputManager> GetInputManager() const { return inputManager; }
		std::shared_ptr<SceneSystem> GetSceneSystem() const { return sceneSystem; }
		std::shared_ptr<CameraSystem> GetCameraSystem() const { return cameraSystem; }
		std::shared_ptr<VulkanRenderer> GetVulkanRenderer() const { return vulkanRenderer; }
		std::shared_ptr<OpenGLRenderer> GetOpenGLRenderer() const { return openglRenderer; }

		unsigned int GetWindowWidth() const { return windowWidth; }
		unsigned int GetWindowHeight() const { return windowHeight; }

		bool IsMinimized() const { return minimized; }

		unsigned int GetTotalFrames() const { return totalFrames; }

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
		bool needResize{ false };
		bool resizing{ false };
		bool fullscreen{ false };
		bool minimized{ false };
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
		std::shared_ptr<VulkanRenderer> vulkanRenderer;
		std::shared_ptr<OpenGLRenderer> openglRenderer;
		std::shared_ptr<CameraSystem> cameraSystem;

	};

}