#pragma once

#include "Engine/EngineConfig.h"
#include "Engine/EngineState.h"
#include "Engine/Machine.h"
#include "Engine/Platform/EditorIpcBridge.h"
#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Jobs/JobSystem.h"
#include "Engine/Systems/IO/CommandSystem.h"
#include "Engine/Systems/Physics/PhysicsSystem.h"
#include "Engine/Systems/Renderer/Renderer.h"

#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace Engine
{

	class CameraSystem;
	class InputManager;
	class OpenGLRenderer;
	class SceneSystem;
	class MeshPool;
	class TexturePool;
	class MaterialPool;
	class FontPool;
	class VulkanRenderer;

	class SwimEngine : public Machine
	{

	public:

		static constexpr EngineState DefaultEngineState = EngineState::Editing;

		explicit SwimEngine(EngineConfig config = {});
		~SwimEngine() override;

		int Start();

		int Awake() override;
		int Init() override;
		int Run();
		void Update(double dt) override;
		void FixedUpdate(unsigned int tickThisSecond) override;
		int Exit() override;
		void Stop();

		void OnEditorCommand(std::string_view msg);

		static EngineConfigParseResult ParseStartingEngineArgs(int argc, char** argv);

		int GetFPS() const;

		const EngineConfig& GetConfig() const { return config; }
		GraphicsBackend GetGraphicsBackend() const { return graphicsBackend; }
		PhysicsBackend GetPhysicsBackend() const { return physicsBackend; }

		Swim::Platform::PlatformSystem& GetPlatformSystem() { return *platformSystem; }
		const Swim::Platform::PlatformSystem& GetPlatformSystem() const { return *platformSystem; }
		Swim::Platform::Window& GetWindow() { return *engineWindow; }
		const Swim::Platform::Window& GetWindow() const { return *engineWindow; }

		InputManager* GetInputManager() { return inputManager.get(); }
		PhysicsSystem* GetPhysicsSystem() { return physicsSystem.get(); }
		SceneSystem* GetSceneSystem() { return sceneSystem.get(); }
		CameraSystem* GetCameraSystem() { return cameraSystem.get(); }
		CommandSystem* GetCommandSystem() { return commandSystem.get(); }
		VulkanRenderer* GetVulkanRenderer() { return vulkanRenderer.get(); }
		OpenGLRenderer* GetOpenGLRenderer() { return openglRenderer.get(); }
		MeshPool* GetMeshPool() { return meshPool.get(); }
		TexturePool* GetTexturePool() { return texturePool.get(); }
		MaterialPool* GetMaterialPool() { return materialPool.get(); }
		FontPool* GetFontPool() { return fontPool.get(); }
		Swim::Jobs::JobSystem* GetJobSystem() { return jobSystem.get(); }

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

		void Create();
		void RegisterVanillaEngineCommands();
		int HeartBeat();
		bool MakeWindow();
		bool ValidateBackendConfiguration() const;
		int AwakeSystems();
		int InitSystems();
		int ExitSystems();
		void UpdateSystems(double dt);
		void FixedUpdateSystems(unsigned int tickThisSecond);
		void HandleWindowEvent(const Swim::Platform::WindowEvent& event);
		void UpdateWindowSize();
		std::string GetWindowTitle() const;

		EngineConfig config{};
		GraphicsBackend graphicsBackend{ GraphicsBackend::Vulkan };
		PhysicsBackend physicsBackend{ PhysicsBackend::PhysX };

		bool uncappedFPS{ true };
		unsigned int targetFPS{ 60 };
		unsigned int totalFrames{ 0 };
		unsigned int tickRate{ 60 };
		double frameTime{ 0.0 };
		double delta{ 0.0 };
		double fpsTimeAccumulator{ 0.0 };
		int fpsFrameCounter{ 0 };
		bool running{ false };
		bool needResize{ false };
		bool resizing{ false };
		bool fullscreen{ false };
		bool minimized{ false };
		bool cursorVisible{ true };
		bool debugging{ false };
		int fps{ 0 };

		EngineState engineState{ EngineState::Editing };

		unsigned int windowWidth{ 1280 };
		unsigned int windowHeight{ 720 };
		bool ownsWindow{ true };

		std::unique_ptr<Swim::Platform::PlatformSystem> platformSystem;
		std::unique_ptr<Swim::Platform::Window> engineWindow;
		std::unique_ptr<Swim::Platform::EditorIpcBridge> editorIpcBridge;
		std::unique_ptr<Swim::Jobs::JobSystem> jobSystem;

		// Core systems have unique ownership. Legacy consumers receive non-owning
		// pointers whose lifetime is bounded by SwimEngine's explicit shutdown order.
		std::unique_ptr<InputManager> inputManager;
		std::unique_ptr<CommandSystem> commandSystem;
		std::unique_ptr<SceneSystem> sceneSystem;
		std::unique_ptr<VulkanRenderer> vulkanRenderer;
		std::unique_ptr<OpenGLRenderer> openglRenderer;
		std::unique_ptr<CameraSystem> cameraSystem;
		std::unique_ptr<PhysicsSystem> physicsSystem;

		std::unique_ptr<MeshPool> meshPool;
		std::unique_ptr<TexturePool> texturePool;
		std::unique_ptr<MaterialPool> materialPool;
		std::unique_ptr<FontPool> fontPool;
		RendererRuntimeServices rendererRuntimeServices{};

	};

}
