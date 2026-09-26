#pragma once

#include "Engine/Assets/AssetSystem.h"
#include "Engine/Commands/CommandRegistry.h"
#include "Engine/EngineConfig.h"
#include "Engine/EngineState.h"
#include "Engine/IO/AsyncIoService.h"
#include "Engine/Input/InputSystem.h"
#include "Engine/Jobs/JobSystem.h"
#include "Engine/Machine.h"
#include "Engine/Memory/FrameArena.h"
#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Components/Tags.h"
#include "Engine/Runtime/EngineStateMachine.h"
#include "Engine/Runtime/SimulationClock.h"
#include "Engine/Systems/Physics/PhysicsSystem.h"
#include "Engine/Systems/Renderer/Runtime/RenderServices.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace Engine
{
	class CameraSystem;
	class FrameRenderer;
	class RenderDevice;
	class SceneRenderBridge;
	class SceneSystem;
	class UiRuntime;

	// The engine runtime (Phase 22/23). Owns the platform window, the core services
	// (jobs, async IO, assets, frame memory, input, commands), the engine state machine
	// (Playing / Paused / Stopped) and the simulation clock (pause, time scale, single
	// steps, a fixed physics rate), physics, the main camera, the modern renderer
	// (RenderDevice + FrameRenderer + the scene render bridge), the UI runtime and the
	// scene system.
	//
	// One frame: pump platform events -> advance input -> advance the clock -> scene
	// BeginFrame (deferred scene switch / Stop reset) -> renderer BeginFrame (previous GPU
	// frame retired) -> UI canvases + input routing -> fixed steps (behaviours, then the
	// physics step with collision callbacks) -> physics interpolation -> Update ->
	// camera components -> render bridge -> UI draw list -> render + present.
	//
	// The renderer is Vulkan through the RHI; there is no OpenGL path and no editor.
	class SwimEngine : public Machine
	{
	  public:
		explicit SwimEngine(EngineConfig config = {});
		~SwimEngine() override;

		SwimEngine(const SwimEngine&) = delete;
		SwimEngine& operator=(const SwimEngine&) = delete;

		// Awake + Init; non-zero (after cleaning up) on failure.
		int Start();
		// Runs frames until the window closes, Quit is called or MaxFrames elapse; then Exit.
		int Run();
		// Runs exactly one frame (tests and tools). False once the engine should stop.
		bool Tick();

		void Quit() { running = false; }

		// True once a "camera" command placed the main camera.
		bool IsCameraPlacedByCommand() const { return cameraLocked; }

		int Awake() override;
		int Init() override;
		void Update(double dt) override;
		void FixedUpdate(unsigned int tickThisSecond) override;
		int Exit() override;

		static EngineConfigParseResult ParseStartingEngineArgs(int argc, char** argv);

		const EngineConfig& GetConfig() const { return config; }

		GraphicsBackend GetGraphicsBackend() const { return graphicsBackend; }

		PhysicsBackend GetPhysicsBackend() const { return physicsBackend; }

		int GetFPS() const { return fps; }

		std::uint64_t GetFrameCount() const { return totalFrames; }

		// Engine state (Playing / Paused / Stopped) and time.
		EngineStateMachine& GetStateMachine() { return stateMachine; }

		EngineState GetEngineState() const { return stateMachine.Get(); }

		SimulationClock& GetClock() { return clock; }

		const SimulationFrame& GetTime() const { return currentFrame; }

		Swim::Platform::PlatformSystem& GetPlatformSystem() { return *platformSystem; }

		Swim::Platform::Window* GetWindow() { return engineWindow.get(); }

		Swim::Input::InputSystem* GetInputSystem() { return inputSystem.get(); }

		Swim::Commands::CommandRegistry* GetCommandRegistry() { return commandRegistry.get(); }

		PhysicsSystem* GetPhysicsSystem() { return physicsSystem.get(); }

		SceneSystem* GetSceneSystem() { return sceneSystem.get(); }

		CameraSystem* GetCameraSystem() { return cameraSystem.get(); }

		FrameRenderer* GetRenderer() { return frameRenderer.get(); }

		SceneRenderBridge* GetRenderBridge() { return renderBridge.get(); }

		UiRuntime* GetUiRuntime() { return uiRuntime.get(); }

		const RenderServices& GetRenderServices() const { return renderServices; }

		TagRegistry& GetTagRegistry() { return tagRegistry; }

		Swim::Jobs::JobSystem* GetJobSystem() { return jobSystem.get(); }

		Swim::IO::AsyncIoService* GetIoSystem() { return ioSystem.get(); }

		Swim::Assets::AssetSystem* GetAssetSystem() { return assetSystem.get(); }

		Swim::Memory::FrameArena& GetFrameArena() { return frameArena; }

		std::uint32_t GetSurfaceWidth() const { return surfaceWidth; }

		std::uint32_t GetSurfaceHeight() const { return surfaceHeight; }

		bool IsMinimized() const { return minimized; }

		// Saves the next rendered frame (binary PPM). False without a renderer.
		bool RequestCapture(std::filesystem::path path);

	  private:
		bool ValidateBackendConfiguration();
		bool MakeWindow();
		int InitRenderer();
		void RegisterEngineCommands();
		void HandleWindowEvent(const Swim::Platform::WindowEvent& event);
		void UpdateSurfaceSize();
		void ApplyCameraComponents();
		void UpdateTextInput();
		std::string GetWindowTitle() const;
		std::filesystem::path FindResourceRoot() const;

		EngineConfig config{};
		GraphicsBackend graphicsBackend{ GraphicsBackend::Vulkan };
		PhysicsBackend physicsBackend{ PhysicsBackend::Jolt };

		EngineStateMachine stateMachine;
		SimulationClock clock;
		SimulationFrame currentFrame{};
		TagRegistry tagRegistry;

		std::uint64_t totalFrames{ 0 };
		unsigned int tickCounter{ 1 };
		double fpsTimeAccumulator{ 0.0 };
		int fpsFrameCounter{ 0 };
		int fps{ 0 };
		bool running{ false };
		bool started{ false };
		bool minimized{ false };
		bool ownsWindow{ true };
		bool textInputActive{ false };
		bool cameraLocked{ false }; // Placed by a "camera" command (scenes keep their hands off).
		bool havePreviousTime{ false };
		std::chrono::steady_clock::time_point previousTime{};
		std::uint32_t surfaceWidth{ 1280 };
		std::uint32_t surfaceHeight{ 720 };
		std::filesystem::path pendingCapture;

		std::unique_ptr<Swim::Platform::PlatformSystem> platformSystem;
		std::unique_ptr<Swim::Platform::Window> engineWindow;
		std::unique_ptr<Swim::Jobs::JobSystem> jobSystem;
		std::unique_ptr<Swim::IO::AsyncIoService> ioSystem;
		std::unique_ptr<Swim::Assets::AssetSystem> assetSystem;
		Swim::Memory::FrameArena frameArena;

		// Destroyed in reverse dependency order by Exit (scenes before the renderer and
		// its services).
		std::unique_ptr<Swim::Input::InputSystem> inputSystem;
		std::unique_ptr<Swim::Commands::CommandRegistry> commandRegistry;
		std::unique_ptr<SceneSystem> sceneSystem;
		std::unique_ptr<CameraSystem> cameraSystem;
		std::unique_ptr<PhysicsSystem> physicsSystem;
		std::unique_ptr<RenderDevice> renderDevice;
		std::unique_ptr<FrameRenderer> frameRenderer;
		std::unique_ptr<SceneRenderBridge> renderBridge;
		std::unique_ptr<UiRuntime> uiRuntime;
		RenderServices renderServices{};
	};
} // namespace Engine
