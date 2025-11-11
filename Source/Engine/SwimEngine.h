#pragma once

#include "Systems/SystemManager.h"
#include "Systems/Renderer/Renderer.h"
#include "Systems/IO/CommandSystem.h"
#include "EngineState.h"
#include <utility>
#include <format>

namespace Engine
{

	// forward declare
	class VulkanRenderer;
	class OpenGLRenderer;

	// std::enable_shared_from_this<SwimEngine> so we can get a pointer to ourselves
	class SwimEngine : public Machine, public std::enable_shared_from_this<SwimEngine>
	{

	private:

		// The initial state the engine will start in if no argument is provided from main
		static constexpr EngineState DefaultEngineState = EngineState::Playing;

	public:

		enum RenderContext
		{
			Vulkan, OpenGL /*, DirectX12, Metal */ // everything commented out is not implemented yet/doesn't need to be implemented
		};

		struct EngineArgs
		{
			EngineArgs(HWND h = nullptr, EngineState s = EngineState::Playing) : parentHandle(h), state(s) {}

			HWND parentHandle{ nullptr };
			EngineState state{ EngineState::Playing };
		};

		// The render context we are using, this should be changed before compliation before building for the target platform.
		// In a fancier world this value would be auto changed via build script steps when doing a batch build for all platforms.
		// This is constexpr so branching logic based on API specific code is instant (Mesh, Texture, etc). 
		static constexpr RenderContext CONTEXT = RenderContext::Vulkan;
		// If we are using the OpenGL context, then this flag determines if we use the shader toy version of the opengl renderer 
		static constexpr bool useShaderToyIfOpenGL = false;

		SwimEngine(EngineArgs args);
		SwimEngine(HWND parentHandle = nullptr, EngineState state = EngineState::Playing);

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

		void OnEditorCommand(const std::wstring& msg);

		static std::shared_ptr<SwimEngine> GetInstance();
		static std::shared_ptr<SwimEngine>& GetInstanceRef();

		static EngineArgs ParseStartingEngineArgs(int argc, char** argv);

		static std::string GetExecutableDirectory();

		int GetFPS() const;

		HWND GetWindowHandle() const { return engineWindowHandle; }

		std::shared_ptr<InputManager>& GetInputManager() { return inputManager; }
		std::shared_ptr<SceneSystem>& GetSceneSystem() { return sceneSystem; }
		std::shared_ptr<CameraSystem>& GetCameraSystem() { return cameraSystem; }
		std::shared_ptr<CommandSystem>& GetCommandSystem() { return commandSystem; }
		std::shared_ptr<VulkanRenderer>& GetVulkanRenderer() { return vulkanRenderer; }
		std::shared_ptr<OpenGLRenderer>& GetOpenGLRenderer() { return openglRenderer; }

		Renderer& GetRenderer();

		unsigned int GetWindowWidth() const { return windowWidth; }
		unsigned int GetWindowHeight() const { return windowHeight; }

		bool IsMinimized() const { return minimized; }

		unsigned int GetTotalFrames() const { return totalFrames; }

		// Returns the amount of time between the previous frame
		double GetDeltaTime() const { return delta; }

		// Stuff will royally screw up if you are passing a masked together value instead of just one specific state flag
		void SetEngineState(EngineState state) { engineState = state; }

		EngineState GetEngineState() const { return engineState; }

		// Send a wide string back to the editor panel. Returns true if the editor handled it (nonzero LRESULT).
		bool SendEditorMessage(const std::wstring& msg, std::uintptr_t channel = 1);

		// Convenience formatter
		template<typename... Args>
		bool SendEditorMessageF(std::wstring_view fmt, Args&&... args)
		{
			std::wstring s = std::vformat(fmt, std::make_wformat_args(std::forward<Args>(args)...));
			return SendEditorMessage(s);
		}

	private:

		void Create(HWND parentHandle, EngineState state);

		void RegisterVanillaEngineCommands();

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
		int fps{ 0 };

		// This state will never be masked, only masked against. I.E will only be Playing or Stopped or Paused etc and never some combined state.
		EngineState engineState{ EngineState::Playing };

		// Window fields
		HWND engineWindowHandle{ nullptr };
		HWND parentHandle{ nullptr };
		HINSTANCE hInstance{ nullptr };
		unsigned int windowWidth{ 1280 };
		unsigned int windowHeight{ 720 };
		std::wstring windowTitle{ L"Demo" };
		std::wstring windowClassName{ L"SwimEngine" };
		bool ownsWindow{ true };

		// Window message handler
		static LRESULT CALLBACK StaticWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
		LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

		// Private Window Helpers
		void UpdateWindowSize();

		std::unique_ptr<SystemManager> systemManager;
		// You would think these should all be unique, but we use them a lot everywhere as fields in scenes and behaviors.
		std::shared_ptr<InputManager> inputManager;
		std::shared_ptr<CommandSystem> commandSystem;
		std::shared_ptr<SceneSystem> sceneSystem;
		std::shared_ptr<VulkanRenderer> vulkanRenderer;
		std::shared_ptr<OpenGLRenderer> openglRenderer;
		std::shared_ptr<CameraSystem> cameraSystem;

	};

}