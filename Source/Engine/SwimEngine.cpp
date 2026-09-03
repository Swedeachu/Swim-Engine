#include "PCH.h"
#include "SwimEngine.h"
#include "Engine/Platform/MonotonicClock.h"
#include "Engine/Systems/Renderer/Vulkan/VulkanRenderer.h"
#include "Engine/Systems/Renderer/OpenGL/OpenGLRenderer.h"
#include "Engine/Systems/Renderer/OpenGL/ShaderToyRendererGL.h"
#include "Engine/Systems/Renderer/Core/Meshes/MeshPool.h"
#include "Engine/Systems/Renderer/Core/Textures/TexturePool.h"
#include "Engine/Systems/Renderer/Core/Material/MaterialPool.h"
#include "Engine/Systems/Renderer/Core/Font/FontPool.h"

#include <filesystem>
#include <stdexcept>

namespace Engine
{

	namespace
	{

		int ReportLifecycleFailure(std::string_view phase, std::string_view systemName, int result)
		{
			if (result != 0)
			{
				std::cerr << "[Engine] " << phase << " failed for " << systemName << " with code " << result << ".\n";
			}
			return result;
		}

	}

	SwimEngine::SwimEngine(EngineConfig config)
		: config(std::move(config))
	{
		Create();
	}

	SwimEngine::~SwimEngine() = default;

	void SwimEngine::Create()
	{
		graphicsBackend = ResolveGraphicsBackend(config.Graphics);
		physicsBackend = ResolvePhysicsBackend(config.Physics);
		platformSystem = std::make_unique<Swim::Platform::PlatformSystem>();
		engineState = config.InitialState;
		windowWidth = config.Window.Width;
		windowHeight = config.Window.Height;
		ownsWindow = !config.Window.ExternalWindow.IsValid() && !config.Window.ExternalParent.IsValid();
	}

	EngineConfigParseResult SwimEngine::ParseStartingEngineArgs(int argc, char** argv)
	{
		return ParseEngineConfigArgs(argc, argv);
	}

	int SwimEngine::Start()
	{
		if (!ValidateBackendConfiguration())
		{
			return -1;
		}

		const int awakeResult = Awake();
		if (awakeResult != 0)
		{
			Exit();
			return awakeResult;
		}

		const int initResult = Init();
		if (initResult != 0)
		{
			Exit();
			return initResult;
		}

		return 0;
	}

	bool SwimEngine::ValidateBackendConfiguration() const
	{
		if (graphicsBackend != GraphicsBackend::Vulkan && graphicsBackend != GraphicsBackend::OpenGLLegacy)
		{
			std::cerr << "[Engine] Graphics backend '" << ToString(graphicsBackend)
				<< "' is configured but does not have an implementation yet.\n";
			return false;
		}

		if (physicsBackend != PhysicsBackend::PhysX)
		{
			std::cerr << "[Engine] Physics backend '" << ToString(physicsBackend)
				<< "' is configured but does not have an implementation yet.\n";
			return false;
		}

		if (config.UseOpenGLShaderToy && graphicsBackend != GraphicsBackend::OpenGLLegacy)
		{
			std::cerr << "[Engine] --opengl-shadertoy requires --graphics=opengl.\n";
			return false;
		}

		return true;
	}

	int SwimEngine::Awake()
	{
		if (!MakeWindow())
		{
			return -1;
		}
		return 0;
	}

	std::string SwimEngine::GetWindowTitle() const
	{
		std::string title = config.Window.Title.empty() ? "Swim Engine" : config.Window.Title;
		title += " [";
		title += ToString(graphicsBackend);
		title += "]";

	#if defined(_SWIM_DEBUG)
		title += " (Debug)";
	#else
		title += " (Release)";
	#endif

		return title;
	}

	bool SwimEngine::MakeWindow()
	{
		Swim::Platform::PlatformDesc platformDesc{};
		platformDesc.OrganizationName = "Swim Services";
		platformDesc.ApplicationName = "Swim Engine";
		if (!platformSystem->Initialize(platformDesc))
		{
			std::cerr << "[Engine] Platform initialization failed.\n";
			return false;
		}

		Swim::Platform::WindowDesc windowDesc = config.Window;
		windowDesc.Title = GetWindowTitle();

		switch (graphicsBackend)
		{
			case GraphicsBackend::Vulkan:
				windowDesc.GraphicsSupport = Swim::Platform::WindowGraphicsSupport::Vulkan;
				break;
			case GraphicsBackend::OpenGLLegacy:
				windowDesc.GraphicsSupport = Swim::Platform::WindowGraphicsSupport::OpenGL;
				break;
			default:
				windowDesc.GraphicsSupport = Swim::Platform::WindowGraphicsSupport::None;
				break;
		}

		engineWindow = platformSystem->GetWindowSystem().Create(windowDesc);
		if (!engineWindow)
		{
			std::cerr << "[Engine] Window creation failed.\n";
			return false;
		}

		engineWindow->Show();
		UpdateWindowSize();

		if (config.Window.ExternalParent.IsValid())
		{
			if (config.Window.ExternalParent.Type != Swim::Platform::NativeWindowType::Win32)
			{
				std::cerr << "[Engine] Legacy editor IPC currently supports Win32 external parents only.\n";
				return false;
			}

			editorIpcBridge = std::make_unique<Swim::Platform::EditorIpcBridge>();
			if (!editorIpcBridge->Initialize(*engineWindow, config.Window.ExternalParent, [this](std::string_view message)
			{
				OnEditorCommand(message);
			}))
			{
				std::cerr << "[Engine] Editor IPC bridge initialization failed.\n";
				return false;
			}
		}

		minimized = engineWindow->IsMinimized();
		needResize = true;
		return true;
	}

	void SwimEngine::HandleWindowEvent(const Swim::Platform::WindowEvent& event)
	{
		if (engineWindow && event.Window != 0 && event.Window != engineWindow->GetId())
		{
			return;
		}

		if (inputManager)
		{
			inputManager->ProcessWindowEvent(event);
		}

		using Swim::Platform::WindowEventType;
		switch (event.Type)
		{
			case WindowEventType::CloseRequested:
				running = false;
				break;

			case WindowEventType::Minimized:
				minimized = true;
				break;

			case WindowEventType::Restored:
			case WindowEventType::Maximized:
				minimized = false;
				UpdateWindowSize();
				needResize = true;
				break;

			case WindowEventType::Resized:
			case WindowEventType::PixelSizeChanged:
			case WindowEventType::DpiScaleChanged:
				UpdateWindowSize();
				if (windowWidth > 0 && windowHeight > 0)
				{
					needResize = true;
				}
				break;

			default:
				break;
		}
	}

	void SwimEngine::OnEditorCommand(std::string_view msg)
	{
		if (!commandSystem)
		{
			return;
		}

		const std::string command(msg);
		const bool ok = commandSystem->ParseAndDispatch(command);
		SendEditorMessage(std::string(ok ? "(Recv [200]): " : "(Recv [400]): ") + command);
	}

	bool SwimEngine::SendEditorMessage(const std::string& msg, std::uintptr_t channel)
	{
		return editorIpcBridge && editorIpcBridge->Send(msg, channel);
	}

	Renderer& SwimEngine::GetRenderer()
	{
		switch (graphicsBackend)
		{
			case GraphicsBackend::Vulkan:
				return *vulkanRenderer;
			case GraphicsBackend::OpenGLLegacy:
				return *openglRenderer;
			default:
				throw std::runtime_error("Configured graphics backend has no renderer instance.");
		}
	}

	int SwimEngine::Init()
	{
		jobSystem = std::make_unique<Swim::Jobs::JobSystem>();
		Swim::Jobs::JobSystemDesc jobDesc{};
		jobDesc.BlockingThreads = 1;
		if (!jobSystem->Initialize(jobDesc))
		{
			std::cerr << "[Engine] Failed to initialize JobSystem.\n";
			return -1;
		}

		inputManager = std::make_unique<InputManager>();
		commandSystem = std::make_unique<CommandSystem>();
		sceneSystem = std::make_unique<SceneSystem>();
		physicsSystem = std::make_unique<PhysicsSystem>();

		switch (graphicsBackend)
		{
			case GraphicsBackend::Vulkan:
				vulkanRenderer = std::make_unique<VulkanRenderer>();
				vulkanRenderer->Create(*engineWindow, windowWidth, windowHeight);
				break;

			case GraphicsBackend::OpenGLLegacy:
				if (config.UseOpenGLShaderToy)
				{
					openglRenderer = std::make_unique<ShaderToyRendererGL>();
				}
				else
				{
					openglRenderer = std::make_unique<OpenGLRenderer>();
				}
				openglRenderer->Create(*engineWindow, windowWidth, windowHeight);
				break;

			default:
				return -1;
		}

		cameraSystem = std::make_unique<CameraSystem>();
		cameraSystem->SetGraphicsBackend(graphicsBackend);
		cameraSystem->SetSurfaceSize(windowWidth, windowHeight);

		Renderer& renderer = GetRenderer();
		meshPool = std::make_unique<MeshPool>(renderer);

		TextureRuntimeContext textureContext{};
		textureContext.Backend = graphicsBackend;
		textureContext.Vulkan = vulkanRenderer.get();
		textureContext.Lifetime = std::make_shared<TextureLifetimeTracker>();
		texturePool = std::make_unique<TexturePool>(platformSystem->GetFileSystem(), std::move(textureContext));

		materialPool = std::make_unique<MaterialPool>(
			*meshPool,
			*texturePool,
			[this](const std::string& message, std::uintptr_t channel)
			{
				return SendEditorMessage(message, channel);
			}
		);
		fontPool = std::make_unique<FontPool>(platformSystem->GetFileSystem(), *texturePool);

		rendererRuntimeServices.Files = &platformSystem->GetFileSystem();
		rendererRuntimeServices.Jobs = jobSystem.get();
		rendererRuntimeServices.Meshes = meshPool.get();
		rendererRuntimeServices.Textures = texturePool.get();
		rendererRuntimeServices.Materials = materialPool.get();
		rendererRuntimeServices.Fonts = fontPool.get();
		renderer.SetRuntimeServices(&rendererRuntimeServices);

		SceneSystemServices sceneServices{};
		sceneServices.Input = inputManager.get();
		sceneServices.Commands = commandSystem.get();
		sceneServices.Camera = cameraSystem.get();
		sceneServices.Vulkan = vulkanRenderer.get();
		sceneServices.OpenGL = openglRenderer.get();
		sceneServices.Meshes = meshPool.get();
		sceneServices.Textures = texturePool.get();
		sceneServices.Materials = materialPool.get();
		sceneServices.Fonts = fontPool.get();
		sceneServices.Files = &platformSystem->GetFileSystem();
		sceneServices.Jobs = jobSystem.get();
		sceneServices.State = &engineState;
		sceneServices.ClipDepth = graphicsBackend == GraphicsBackend::Vulkan
			? ClipSpaceDepthRange::ZeroToOne
			: ClipSpaceDepthRange::MinusOneToOne;
		sceneServices.SendEditorMessage = [this](const std::string& message, std::uintptr_t channel)
		{
			return SendEditorMessage(message, channel);
		};
		sceneServices.GetFPS = [this]()
		{
			return GetFPS();
		};
		sceneSystem->SetServices(std::move(sceneServices));
		physicsSystem->SetServices(sceneSystem.get(), &engineState);

		if (vulkanRenderer)
		{
			vulkanRenderer->SetCameraSystem(cameraSystem.get());
			vulkanRenderer->SetSceneSystem(sceneSystem.get());
		}
		if (openglRenderer)
		{
			openglRenderer->SetCameraSystem(cameraSystem.get());
			openglRenderer->SetSceneSystem(sceneSystem.get());
		}

		const int awakeResult = AwakeSystems();
		if (awakeResult != 0)
		{
			return awakeResult;
		}

		const int initResult = InitSystems();
		if (initResult != 0)
		{
			return initResult;
		}

		Swim::Platform::WindowEvent initialWindowEvent{};
		initialWindowEvent.Window = engineWindow->GetId();
		initialWindowEvent.LogicalSize = engineWindow->GetLogicalSize();
		initialWindowEvent.PixelSize = engineWindow->GetPixelSize();
		initialWindowEvent.DpiScale = engineWindow->GetDpiScale();
		initialWindowEvent.Type = engineWindow->IsFocused()
			? Swim::Platform::WindowEventType::FocusGained
			: Swim::Platform::WindowEventType::FocusLost;
		inputManager->ProcessWindowEvent(initialWindowEvent);

		RegisterVanillaEngineCommands();
		return 0;
	}

	int SwimEngine::AwakeSystems()
	{
		int result = inputManager->Awake();
		if (result != 0)
		{
			return ReportLifecycleFailure("Awake", "InputManager", result);
		}

		result = commandSystem->Awake();
		if (result != 0)
		{
			return ReportLifecycleFailure("Awake", "CommandSystem", result);
		}

		result = physicsSystem->Awake();
		if (result != 0)
		{
			return ReportLifecycleFailure("Awake", "PhysicsSystem", result);
		}

		result = cameraSystem->Awake();
		if (result != 0)
		{
			return ReportLifecycleFailure("Awake", "CameraSystem", result);
		}

		result = GetRenderer().Awake();
		if (result != 0)
		{
			return ReportLifecycleFailure("Awake", "Renderer", result);
		}

		// Scenes are consumers of input, command, physics, camera, and renderer
		// services, so they are deliberately the last core runtime owner awakened.
		result = sceneSystem->Awake();
		if (result != 0)
		{
			return ReportLifecycleFailure("Awake", "SceneSystem", result);
		}

		return 0;
	}

	int SwimEngine::InitSystems()
	{
		int result = inputManager->Init();
		if (result != 0)
		{
			return ReportLifecycleFailure("Init", "InputManager", result);
		}

		result = commandSystem->Init();
		if (result != 0)
		{
			return ReportLifecycleFailure("Init", "CommandSystem", result);
		}

		result = physicsSystem->Init();
		if (result != 0)
		{
			return ReportLifecycleFailure("Init", "PhysicsSystem", result);
		}

		result = cameraSystem->Init();
		if (result != 0)
		{
			return ReportLifecycleFailure("Init", "CameraSystem", result);
		}

		result = GetRenderer().Init();
		if (result != 0)
		{
			return ReportLifecycleFailure("Init", "Renderer", result);
		}

		result = sceneSystem->Init();
		if (result != 0)
		{
			return ReportLifecycleFailure("Init", "SceneSystem", result);
		}

		return 0;
	}

	void SwimEngine::RegisterVanillaEngineCommands()
	{
		SwimEngine* self = this;

		auto summarize = [](SwimEngine* e)
		{
			std::string s = "[Engine] State ->";
			if (HasAnyEngineStates(e->engineState, EngineState::Playing))
			{
				s += " Playing";
			}
			if (HasAnyEngineStates(e->engineState, EngineState::Paused))
			{
				s += " Paused";
			}
			if (HasAnyEngineStates(e->engineState, EngineState::Editing))
			{
				s += " Editing";
			}
			if (HasAnyEngineStates(e->engineState, EngineState::Stopped))
			{
				s += " Stopped";
			}
			if (!HasAnyEngineStates(e->engineState, EngineState::All))
			{
				s += " None";
			}
			e->SendEditorMessage(s);
		};

		commandSystem->RegisterRaw("play", [self, summarize](const std::vector<std::string>&)
		{
			self->engineState &= ~EngineState::Stopped;
			self->engineState |= EngineState::Playing;
			summarize(self);
		});

		commandSystem->RegisterRaw("pause", [self, summarize](const std::vector<std::string>&)
		{
			self->engineState |= EngineState::Paused;
			summarize(self);
		});

		commandSystem->RegisterRaw("resume", [self, summarize](const std::vector<std::string>&)
		{
			self->engineState &= ~EngineState::Paused;
			summarize(self);
		});

		commandSystem->RegisterRaw("stop", [self, summarize](const std::vector<std::string>&)
		{
			self->engineState &= ~EngineState::Playing;
			self->engineState |= EngineState::Stopped;
			summarize(self);
		});

		commandSystem->RegisterRaw("edit", [self, summarize](const std::vector<std::string>&)
		{
			self->engineState |= EngineState::Editing;
			summarize(self);
		});

		commandSystem->RegisterRaw("game", [self, summarize](const std::vector<std::string>&)
		{
			self->engineState &= ~EngineState::Editing;
			summarize(self);
		});

		commandSystem->RegisterRaw("restart", [self](const std::vector<std::string>&)
		{
			self->SendEditorMessage("[Engine] Restart requested (not implemented)");
		});
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
		running = true;

		auto previousTime = Swim::Platform::MonotonicClock::Now();
		double accumulatedTime = 0.0;
		double fixedTimeStep = 1.0 / tickRate;
		unsigned int tickCounter = 1;
		const double maxDeltaTime = 5.0 * fixedTimeStep;

		while (running)
		{
			platformSystem->PumpEvents(
				[this](const Swim::Platform::WindowEvent& event)
				{
					HandleWindowEvent(event);
				},
				[this](const Swim::Platform::InputEvent& event)
				{
					if (!inputManager || !engineWindow || (event.Window != 0 && event.Window != engineWindow->GetId()))
					{
						return;
					}
					inputManager->ProcessInputEvent(event);
				}
			);

			if (!running)
			{
				break;
			}

			auto currentTime = Swim::Platform::MonotonicClock::Now();
			delta = Swim::Platform::MonotonicClock::SecondsBetween(previousTime, currentTime);
			previousTime = currentTime;

			if (delta > maxDeltaTime)
			{
				std::cerr << "Frame skipped due to excessive delta time: " << delta << " seconds.\n";
				accumulatedTime = 0.0;
				continue;
			}

			accumulatedTime += delta;
			Transform::BeginFrameDirtyTracking();

			while (accumulatedTime >= fixedTimeStep)
			{
				FixedUpdate(tickCounter);
				accumulatedTime -= fixedTimeStep;
				tickCounter++;
				if (tickCounter > tickRate)
				{
					tickCounter = 1;
				}
			}

			Update(delta);
			++totalFrames;
		}

		return Exit();
	}

	void SwimEngine::Update(double dt)
	{
		if (engineWindow && engineWindow->IsExternal())
		{
			const unsigned int previousWidth = windowWidth;
			const unsigned int previousHeight = windowHeight;
			engineWindow->SyncExternalParentSize();
			UpdateWindowSize();
			if (previousWidth != windowWidth || previousHeight != windowHeight)
			{
				needResize = true;
			}
		}

		if (!minimized && needResize)
		{
			switch (graphicsBackend)
			{
				case GraphicsBackend::Vulkan:
					if (vulkanRenderer) { vulkanRenderer->SetFramebufferResized(); }
					break;
				case GraphicsBackend::OpenGLLegacy:
					if (openglRenderer) { openglRenderer->SetFramebufferResized(); }
					break;
				default:
					break;
			}

			needResize = false;
		}

		if (jobSystem && jobSystem->IsRunning())
		{
			jobSystem->RunMainThreadJobs();
		}

		UpdateSystems(dt);

		if (jobSystem && jobSystem->IsRunning())
		{
			jobSystem->RunMainThreadJobs();
		}

		fpsTimeAccumulator += dt;
		fpsFrameCounter++;

		if (fpsTimeAccumulator >= 1.0)
		{
			fps = static_cast<int>(static_cast<double>(fpsFrameCounter) / fpsTimeAccumulator);

			if (ownsWindow && engineWindow)
			{
				engineWindow->SetTitle(GetWindowTitle() + " | " + std::to_string(fps) + " FPS");
			}

			fpsTimeAccumulator = 0.0;
			fpsFrameCounter = 0;
		}
	}

	void SwimEngine::UpdateSystems(double dt)
	{
		if (inputManager)
		{
			inputManager->Update(dt);
		}
		if (commandSystem)
		{
			commandSystem->Update(dt);
		}
		if (sceneSystem)
		{
			sceneSystem->Update(dt);
		}
		if (physicsSystem)
		{
			physicsSystem->Update(dt);
		}
		if (vulkanRenderer)
		{
			vulkanRenderer->Update(dt);
		}
		if (openglRenderer)
		{
			openglRenderer->Update(dt);
		}
		if (cameraSystem)
		{
			cameraSystem->Update(dt);
		}
	}

	int SwimEngine::GetFPS() const
	{
		return fps;
	}

	void SwimEngine::FixedUpdate(unsigned int tickThisSecond)
	{
		float time = 1.0f / tickRate;
		if (physicsSystem)
		{
			physicsSystem->SetFixedDeltaSeconds(time);
		}

		FixedUpdateSystems(tickThisSecond);
	}

	void SwimEngine::FixedUpdateSystems(unsigned int tickThisSecond)
	{
		if (inputManager)
		{
			inputManager->FixedUpdate(tickThisSecond);
		}
		if (commandSystem)
		{
			commandSystem->FixedUpdate(tickThisSecond);
		}
		if (sceneSystem)
		{
			sceneSystem->FixedUpdate(tickThisSecond);
		}
		if (physicsSystem)
		{
			physicsSystem->FixedUpdate(tickThisSecond);
		}
		if (vulkanRenderer)
		{
			vulkanRenderer->FixedUpdate(tickThisSecond);
		}
		if (openglRenderer)
		{
			openglRenderer->FixedUpdate(tickThisSecond);
		}
		if (cameraSystem)
		{
			cameraSystem->FixedUpdate(tickThisSecond);
		}
	}

	int SwimEngine::ExitSystems()
	{
		int firstError = 0;

		auto exitSystem = [&firstError](std::string_view name, Machine* system)
		{
			if (!system)
			{
				return;
			}

			const int result = system->Exit();
			if (result != 0)
			{
				ReportLifecycleFailure("Exit", name, result);
				if (firstError == 0)
				{
					firstError = result;
				}
			}
		};

		// Complete any work which may still reference scene/renderer state before
		// those owners begin teardown. Blocking IO lanes remain alive until the
		// final JobSystem shutdown below.
		if (jobSystem && jobSystem->IsRunning())
		{
			jobSystem->RunMainThreadJobs();
			jobSystem->WaitForAll();
		}

		// Destroy consumers before the services they reference. Reset each owner
		// immediately after Exit so no dormant Scene/renderer object can retain a
		// non-owning pointer into an already-destroyed dependency.
		exitSystem("SceneSystem", sceneSystem.get());
		sceneSystem.reset();

		if (vulkanRenderer)
		{
			exitSystem("Renderer", vulkanRenderer.get());
			vulkanRenderer.reset();
		}
		else if (openglRenderer)
		{
			exitSystem("Renderer", openglRenderer.get());
			openglRenderer.reset();
		}

		fontPool.reset();
		materialPool.reset();
		texturePool.reset();
		meshPool.reset();
		rendererRuntimeServices = {};

		exitSystem("CameraSystem", cameraSystem.get());
		cameraSystem.reset();

		exitSystem("PhysicsSystem", physicsSystem.get());
		physicsSystem.reset();

		exitSystem("CommandSystem", commandSystem.get());
		commandSystem.reset();

		exitSystem("InputManager", inputManager.get());
		inputManager.reset();

		if (jobSystem)
		{
			jobSystem->Shutdown(Swim::Jobs::JobShutdownMode::Drain);
			jobSystem.reset();
		}

		return firstError;
	}

	int SwimEngine::Exit()
	{
		running = false;
		const int result = ExitSystems();

		if (editorIpcBridge)
		{
			editorIpcBridge->Shutdown();
			editorIpcBridge.reset();
		}

		engineWindow.reset();
		if (platformSystem)
		{
			platformSystem->Shutdown();
		}


		return result;
	}

	void SwimEngine::UpdateWindowSize()
	{
		if (!engineWindow)
		{
			return;
		}

		const Swim::Platform::Extent2D size = engineWindow->GetPixelSize();
		windowWidth = size.Width;
		windowHeight = size.Height;

		if (cameraSystem)
		{
			cameraSystem->SetSurfaceSize(windowWidth, windowHeight);
		}

		switch (graphicsBackend)
		{
			case GraphicsBackend::Vulkan:
				if (vulkanRenderer)
				{
					vulkanRenderer->SetSurfaceSize(windowWidth, windowHeight);
				}
				break;

			case GraphicsBackend::OpenGLLegacy:
				if (openglRenderer)
				{
					openglRenderer->SetSurfaceSize(windowWidth, windowHeight);
				}
				break;

			default:
				break;
		}
	}

}
