#pragma once

#include "Systems/SystemManager.h"
#include "Systems/Renderer/Renderer.h"
#include "Systems/IO/CommandSystem.h"
#include "Systems/Physics/PhysicsSystem.h"
#include "EngineState.h"
#include "Engine/Platform/EditorIpcBridge.h"
#include "Engine/Platform/PlatformSystem.h"
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace Engine
{

	class VulkanRenderer;
	class OpenGLRenderer;

	class SwimEngine : public Machine, public std::enable_shared_from_this<SwimEngine>
	{

	public:

		static constexpr EngineState DefaultEngineState = EngineState::Editing;

		enum RenderContext
		{
			Vulkan, OpenGL
		};

		struct EngineArgs
		{
			EngineArgs(std::uintptr_t externalParent = 0, EngineState s = EngineState::Playing) : externalParentWindow(externalParent), state(s) {}

			std::uintptr_t externalParentWindow{ 0 };
			EngineState state{ EngineState::Playing };
		};

		static constexpr RenderContext CONTEXT = RenderContext::Vulkan;
		static constexpr bool useShaderToyIfOpenGL = false;

		SwimEngine(EngineArgs args);
		SwimEngine(std::uintptr_t externalParentWindow = 0, EngineState state = EngineState::Playing);

		bool Start();

		int Awake() override;
		int Init() override;
		int Run();
		void Update(double dt) override;
		void FixedUpdate(unsigned int tickThisSecond) override;
		int Exit() override;
		void Stop();

		void OnEditorCommand(std::string_view msg);

		static std::shared_ptr<SwimEngine> GetInstance();
		static std::shared_ptr<SwimEngine>& GetInstanceRef();

		static EngineArgs ParseStartingEngineArgs(int argc, char** argv);
		static std::string GetExecutableDirectory();

		int GetFPS() const;

		Swim::Platform::PlatformSystem& GetPlatformSystem() { return *platformSystem; }
		const Swim::Platform::PlatformSystem& GetPlatformSystem() const { return *platformSystem; }
		Swim::Platform::Window& GetWindow() { return *engineWindow; }
		const Swim::Platform::Window& GetWindow() const { return *engineWindow; }

		std::shared_ptr<InputManager>& GetInputManager() { return inputManager; }
		std::shared_ptr<PhysicsSystem>& GetPhysicsSystem() { return physicsSystem; }
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
		double GetDeltaTime() const { return delta; }

		void SetEngineState(EngineState state) { engineState = state; }
		EngineState GetEngineState() const { return engineState; }

		bool SendEditorMessage(const std::string& msg, std::uintptr_t channel = 1);

		template<typename... Args>
		bool SendEditorMessageF(std::string_view fmt, Args&&... args)
		{
			std::string s = std::vformat(fmt, std::make_format_args(std::forward<Args>(args)...));
			return SendEditorMessage(s);
		}

	private:

		void Create(std::uintptr_t externalParentWindow, EngineState state);
		void RegisterVanillaEngineCommands();
		int HeartBeat();
		bool MakeWindow();
		void HandleWindowEvent(const Swim::Platform::WindowEvent& event);
		void UpdateWindowSize();

		bool uncappedFPS{ true };
		unsigned int targetFPS{ 60 };
		unsigned int totalFrames{ 0 };
		unsigned int tickRate{ 60 };
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

		EngineState engineState{ EngineState::Playing };

		std::uintptr_t externalParentWindow{ 0 };
		unsigned int windowWidth{ 1280 };
		unsigned int windowHeight{ 720 };
		std::string windowTitle{ "Demo" };
		bool ownsWindow{ true };

		std::unique_ptr<Swim::Platform::PlatformSystem> platformSystem;
		std::unique_ptr<Swim::Platform::Window> engineWindow;
		std::unique_ptr<Swim::Platform::EditorIpcBridge> editorIpcBridge;

		std::unique_ptr<SystemManager> systemManager;
		std::shared_ptr<InputManager> inputManager;
		std::shared_ptr<CommandSystem> commandSystem;
		std::shared_ptr<SceneSystem> sceneSystem;
		std::shared_ptr<VulkanRenderer> vulkanRenderer;
		std::shared_ptr<OpenGLRenderer> openglRenderer;
		std::shared_ptr<CameraSystem> cameraSystem;
		std::shared_ptr<PhysicsSystem> physicsSystem;

	};

}
