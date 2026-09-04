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
#include "Engine/Systems/Scene/SceneSystem.h"
#include "Engine/Systems/Physics/Backends/PhysX/PhysXBackendFactory.h"

#if SWIM_ENABLE_DEV_ASSET_AUTOCOOK
#include "Tools/AssetCompiler/DevelopmentAssetPipeline.h"
#endif

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

#if SWIM_ENABLE_DEV_ASSET_AUTOCOOK
		const char* DevelopmentAssetErrorStageName(Swim::AssetCompiler::DevelopmentAssetErrorStage stage)
		{
			switch (stage)
			{
				case Swim::AssetCompiler::DevelopmentAssetErrorStage::Inspect:
					return "inspect";
				case Swim::AssetCompiler::DevelopmentAssetErrorStage::Import:
					return "import";
				case Swim::AssetCompiler::DevelopmentAssetErrorStage::Optimize:
					return "optimize";
				case Swim::AssetCompiler::DevelopmentAssetErrorStage::Compile:
					return "compile";
				case Swim::AssetCompiler::DevelopmentAssetErrorStage::Publish:
					return "publish";
				case Swim::AssetCompiler::DevelopmentAssetErrorStage::Load:
					return "load";
			}
			return "unknown";
		}
#endif

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

		// Scene type registration is an application configuration step that happens
		// before Start(). Keep the SceneSystem alive for the full SwimEngine lifetime
		// so pre-start registrations are retained until runtime services are injected.
		sceneSystem = std::make_unique<SceneSystem>();

		engineState = config.InitialState;
		windowWidth = config.Window.Width;
		windowHeight = config.Window.Height;
		ownsWindow = !config.Window.ExternalWindow.IsValid();
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
		if (windowDesc.ExternalParent.IsValid())
		{
			std::cout << "[Engine] Ignoring legacy ExternalParent window embedding; external editor integration is dormant.\n";
			windowDesc.ExternalParent = {};
		}
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

		// Legacy external-editor embedding/WM_COPYDATA IPC is intentionally dormant.
		// Future editor work lives inside the engine UI. Keep EditorIpcBridge in-tree as
		// historical/reference code, but do not initialize or consult it at runtime.

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

		ioSystem = std::make_unique<Swim::IO::AsyncIoService>();
		if (!ioSystem->Initialize(platformSystem->GetFileSystem(), *jobSystem))
		{
			std::cerr << "[Engine] Failed to initialize AsyncIoService.\n";
			return -1;
		}

		assetSystem = std::make_unique<Swim::Assets::AssetSystem>();
		if (!assetSystem->Initialize())
		{
			std::cerr << "[Engine] Failed to initialize AssetSystem.\n";
			return -1;
		}

#if SWIM_ENABLE_DEV_ASSET_AUTOCOOK
		{
			const std::filesystem::path assetRoot = platformSystem->GetFileSystem().GetAssetRoot();
			std::cout << "[Assets] Development asset root: " << assetRoot.string() << '\n';
			const auto bootstrap = Swim::AssetCompiler::RunDevelopmentAssetBootstrap(assetRoot, *assetSystem);
			std::cout << "[Assets] Sources: " << bootstrap.Stats.SourcesDiscovered
				<< ", current: " << bootstrap.Stats.SourcesCurrent
				<< ", cooked: " << bootstrap.Stats.SourcesCooked
				<< ", skipped unsupported: " << bootstrap.Stats.SourcesSkippedUnsupported
				<< ", root models loaded: " << bootstrap.Stats.RootModelsLoaded
				<< ", loaded .sasset files: " << bootstrap.Stats.SassetsLoaded << ".\n";
			for (const auto& error : bootstrap.Errors)
			{
				std::cerr << "[Assets] [" << DevelopmentAssetErrorStageName(error.Stage) << "] "
					<< error.SourcePath.string() << ": " << error.Message << '\n';
			}
			if (!bootstrap.Errors.empty())
			{
				std::cerr << "[Assets] Development bootstrap completed with " << bootstrap.Errors.size()
					<< " error(s). Failed authoring assets will be skipped instead of terminating scene startup.\n";
			}
		}
#endif

		inputManager = std::make_unique<InputManager>();
		commandSystem = std::make_unique<CommandSystem>();
		switch (physicsBackend)
		{
			case PhysicsBackend::PhysX:
				physicsSystem = std::make_unique<PhysicsSystem>(CreatePhysXBackend());
				break;
			default:
				return -1;
		}

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
		cameraSystem->SetSurfaceSize(windowWidth, windowHeight);

		Renderer& renderer = GetRenderer();
		meshPool = std::make_unique<MeshPool>(renderer);

		TextureRuntimeContext textureContext{};
		textureContext.Backend = graphicsBackend;
		textureContext.Vulkan = vulkanRenderer.get();
		textureContext.Lifetime = std::make_shared<TextureLifetimeTracker>();
		texturePool = std::make_unique<TexturePool>(platformSystem->GetFileSystem(), std::move(textureContext));

		materialPool = std::make_unique<MaterialPool>(
			*assetSystem,
			*meshPool,
			*texturePool
		);
		fontPool = std::make_unique<FontPool>(platformSystem->GetFileSystem(), *texturePool);

		rendererRuntimeServices.Files = &platformSystem->GetFileSystem();
		rendererRuntimeServices.Jobs = jobSystem.get();
		rendererRuntimeServices.IO = ioSystem.get();
		rendererRuntimeServices.Assets = assetSystem.get();
		rendererRuntimeServices.FrameMemory = &frameArena;
		rendererRuntimeServices.Meshes = meshPool.get();
		rendererRuntimeServices.Textures = texturePool.get();
		rendererRuntimeServices.Materials = materialPool.get();
		rendererRuntimeServices.Fonts = fontPool.get();
		renderer.SetRuntimeServices(&rendererRuntimeServices);

		SceneSystemServices sceneServices{};
		sceneServices.Presentation.Input = inputManager.get();
		sceneServices.Tools.Commands = commandSystem.get();
		sceneServices.Presentation.Camera = cameraSystem.get();
		// Renderer-owned environment services are created during Renderer::Awake().
		// Bind the cubemap controller after renderer Awake instead of snapshotting nullptr here.
		sceneServices.Presentation.CubeMap = nullptr;
		sceneServices.Presentation.Meshes = meshPool.get();
		sceneServices.Presentation.Textures = texturePool.get();
		sceneServices.Presentation.Materials = materialPool.get();
		sceneServices.Presentation.Fonts = fontPool.get();
		sceneServices.Core.Files = &platformSystem->GetFileSystem();
		sceneServices.Core.Jobs = jobSystem.get();
		sceneServices.Core.IO = ioSystem.get();
		sceneServices.Core.Assets = assetSystem.get();
		sceneServices.Core.FrameMemory = &frameArena;
		sceneServices.Core.State = &engineState;
		sceneServices.Tools.GetFPS = [this]()
		{
			return GetFPS();
		};
		sceneSystem->SetServices(std::move(sceneServices));

		if (vulkanRenderer)
		{
			vulkanRenderer->SetCameraSystem(cameraSystem.get());
		}
		if (openglRenderer)
		{
			openglRenderer->SetCameraSystem(cameraSystem.get());
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

		// The renderer creates its backend-neutral cubemap controller during Awake().
		// Scene services must receive that live controller before any Scene::Awake/Init runs.
		sceneSystem->SetCubeMapController(GetRenderer().GetCubeMapController().get());

		// Scenes are consumers of input, command, physics, camera, and renderer
		// services, so they are deliberately the last core runtime owner awakened.
		result = sceneSystem->Awake();
		if (result != 0)
		{
			return ReportLifecycleFailure("Awake", "SceneSystem", result);
		}

		GetRenderer().SetRenderScene(sceneSystem->GetActiveScene().get());

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
			std::cout << s << std::endl;
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
			std::cout << "[Engine] Restart requested (not implemented)" << std::endl;
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
			frameArena.BeginFrame(static_cast<std::uint64_t>(totalFrames) + 1);
			if (sceneSystem)
			{
				sceneSystem->BeginFrame();
			}

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
		// ExternalParent embedding belonged to the retired external-editor experiment and is
		// deliberately ignored by MakeWindow(). ExternalWindow remains a generic platform
		// capability, but it does not participate in parent-size synchronization.
		if (engineWindow && config.Window.ExternalWindow.IsValid())
		{
			const unsigned int previousWidth = windowWidth;
			const unsigned int previousHeight = windowHeight;
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
		if (ioSystem && ioSystem->IsRunning())
		{
			ioSystem->PumpCompletions();
		}

		UpdateSystems(dt);

		if (jobSystem && jobSystem->IsRunning())
		{
			jobSystem->RunMainThreadJobs();
		}
		if (ioSystem && ioSystem->IsRunning())
		{
			ioSystem->PumpCompletions();
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
		Scene* activeScene = nullptr;
		if (sceneSystem)
		{
			sceneSystem->Update(dt);
			activeScene = sceneSystem->GetActiveScene().get();
		}
		if (physicsSystem && activeScene)
		{
			activeScene->UpdatePhysics(*physicsSystem, dt);
		}

		GetRenderer().SetRenderScene(activeScene);
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
		Scene* activeScene = nullptr;
		if (sceneSystem)
		{
			sceneSystem->FixedUpdate(tickThisSecond);
			activeScene = sceneSystem->GetActiveScene().get();
		}
		if (physicsSystem && activeScene)
		{
			activeScene->FixedUpdatePhysics(*physicsSystem);
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

		// Drain IO first while every consumer and its completion callback target is
		// still alive. AsyncIoService owns no threads; its blocking work runs on
		// JobSystem lanes, so Jobs remains alive until the service is shut down.
		if (ioSystem && ioSystem->IsRunning())
		{
			ioSystem->Shutdown(Swim::IO::IoShutdownMode::Drain);
		}

		// Complete any remaining compute/main-thread work which may still reference
		// scene/renderer state before those owners begin teardown.
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

		if (assetSystem && assetSystem->IsRunning())
		{
			assetSystem->Shutdown();
		}
		assetSystem.reset();

		exitSystem("CameraSystem", cameraSystem.get());
		cameraSystem.reset();

		exitSystem("PhysicsSystem", physicsSystem.get());
		physicsSystem.reset();

		exitSystem("CommandSystem", commandSystem.get());
		commandSystem.reset();

		exitSystem("InputManager", inputManager.get());
		inputManager.reset();

		ioSystem.reset();

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
