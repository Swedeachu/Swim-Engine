#include "Engine/SwimEngine.h"

#include "Engine/Components/CameraComponent.h"
#include "Engine/Components/Transform.h"
#include "Engine/Runtime/UiRuntime.h"
#include "Engine/Systems/Camera/CameraSystem.h"
#include "Engine/Systems/Renderer/Runtime/FrameRenderer.h"
#include "Engine/Systems/Renderer/Runtime/MaterialLibrary.h"
#include "Engine/Systems/Renderer/Runtime/MeshLibrary.h"
#include "Engine/Systems/Renderer/Runtime/RenderDevice.h"
#include "Engine/Systems/Renderer/Runtime/ShaderLibrary.h"
#include "Engine/Systems/Scene/RenderExtraction/Runtime/SceneRenderBridge.h"
#include "Engine/Systems/Scene/SceneSystem.h"

#if SWIM_ENABLE_PHYSX_BACKEND
#include "Engine/Systems/Physics/Backends/PhysX/PhysXBackendFactory.h"
#endif

#if SWIM_ENABLE_JOLT_BACKEND
#include "Engine/Systems/Physics/Backends/Jolt/JoltBackendFactory.h"
#endif

#if SWIM_ENABLE_DEV_ASSET_AUTOCOOK
#include "Tools/AssetCompiler/DevelopmentAssetPipeline.h"
#endif

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
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

		constexpr bool PhysXCompiled =
#if SWIM_ENABLE_PHYSX_BACKEND
			true;
#else
			false;
#endif

		constexpr bool JoltCompiled =
#if SWIM_ENABLE_JOLT_BACKEND
			true;
#else
			false;
#endif

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

		std::array<float, 3> ToArray(const glm::vec3& v)
		{
			return { v.x, v.y, v.z };
		}
	} // namespace

	SwimEngine::SwimEngine(EngineConfig configValue) : config(std::move(configValue)), stateMachine(EngineState::Playing)
	{
		graphicsBackend = ResolveGraphicsBackend(config.Graphics);
		physicsBackend = ResolvePhysicsBackend(config.Physics, PhysXCompiled, JoltCompiled);
		platformSystem = std::make_unique<Swim::Platform::PlatformSystem>();

		// Scene and behaviour types are registered by the application before Start(); the
		// SceneSystem lives for the engine's lifetime so those registrations are kept.
		sceneSystem = std::make_unique<SceneSystem>();
		cameraSystem = std::make_unique<CameraSystem>();

		surfaceWidth = config.Window.Width;
		surfaceHeight = config.Window.Height;
		ownsWindow = !config.Window.ExternalWindow.IsValid();

		clock.SetFixedRate(config.FixedRate);
		clock.SetTimeScale(config.TimeScale);
	}

	SwimEngine::~SwimEngine()
	{
		if (started)
		{
			Exit();
		}
	}

	EngineConfigParseResult SwimEngine::ParseStartingEngineArgs(int argc, char** argv)
	{
		return ParseEngineConfigArgs(argc, argv);
	}

	bool SwimEngine::ValidateBackendConfiguration()
	{
		if (graphicsBackend != GraphicsBackend::Vulkan)
		{
			std::cerr << "[Engine] Graphics backend '" << ToString(graphicsBackend) << "' has no RHI implementation yet; use vulkan.\n";
			return false;
		}
		if (physicsBackend == PhysicsBackend::Auto)
		{
			std::cerr << "[Engine] Physics backend '" << ToString(config.Physics) << "' is not compiled into this build.\n";
			return false;
		}
		if (!IsSingleEngineState(config.InitialState))
		{
			std::cerr << "[Engine] The initial state must be playing, paused or stopped.\n";
			return false;
		}
		return true;
	}

	int SwimEngine::Start()
	{
		if (!ValidateBackendConfiguration())
		{
			return -1;
		}
		started = true;
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
		running = true;
		return 0;
	}

	int SwimEngine::Awake()
	{
		return MakeWindow() ? 0 : -1;
	}

	std::string SwimEngine::GetWindowTitle() const
	{
		std::string title = config.Window.Title.empty() ? "Swim Engine" : config.Window.Title;
		title += " [";
		title += ToString(graphicsBackend);
		title += " | ";
		title += ToString(physicsBackend);
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
		platformDesc.Headless = config.Present == PresentMode::Headless;
		if (!platformSystem->Initialize(platformDesc))
		{
			std::cerr << "[Engine] Platform initialization failed.\n";
			return false;
		}
		if (config.Present == PresentMode::Headless)
		{
			surfaceWidth = config.Window.Width;
			surfaceHeight = config.Window.Height;
			cameraSystem->SetSurfaceSize(surfaceWidth, surfaceHeight);
			return true;
		}

		Swim::Platform::WindowDesc windowDesc = config.Window;
		if (windowDesc.ExternalParent.IsValid())
		{
			std::cout << "[Engine] Ignoring ExternalParent window embedding; the external editor transport is archived.\n";
			windowDesc.ExternalParent = {};
		}
		windowDesc.Title = GetWindowTitle();
		windowDesc.GraphicsSupport = Swim::Platform::WindowGraphicsSupport::Vulkan;
		engineWindow = platformSystem->GetWindowSystem().Create(windowDesc);
		if (!engineWindow)
		{
			std::cerr << "[Engine] Window creation failed.\n";
			return false;
		}
		engineWindow->Show();
		UpdateSurfaceSize();
		minimized = engineWindow->IsMinimized();
		return true;
	}

	std::filesystem::path SwimEngine::FindResourceRoot() const
	{
		const auto& files = platformSystem->GetFileSystem();
		const std::array<std::filesystem::path, 4> candidates{ files.GetExecutableDirectory() / "Resources",
			std::filesystem::current_path() / "Resources", files.GetExecutableDirectory() / ".." / ".." / "Resources",
#if defined(SWIM_RESOURCE_DIR)
			std::filesystem::path(SWIM_RESOURCE_DIR)
#else
			files.GetExecutableDirectory() / ".." / ".." / ".." / "Resources"
#endif
		};
		for (const auto& candidate : candidates)
		{
			std::error_code error;
			if (std::filesystem::is_regular_file(candidate / "Fonts" / "DejaVuSans.ttf", error))
			{
				return candidate;
			}
		}
		return candidates.front();
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
			std::error_code error;
			if (std::filesystem::is_directory(assetRoot, error))
			{
				std::cout << "[Assets] Development asset root: " << assetRoot.string() << '\n';
				const auto bootstrap = Swim::AssetCompiler::RunDevelopmentAssetBootstrap(assetRoot, *assetSystem);
				std::cout << "[Assets] Sources: " << bootstrap.Stats.SourcesDiscovered << ", current: " << bootstrap.Stats.SourcesCurrent
						  << ", cooked: " << bootstrap.Stats.SourcesCooked
						  << ", skipped unsupported: " << bootstrap.Stats.SourcesSkippedUnsupported
						  << ", root models loaded: " << bootstrap.Stats.RootModelsLoaded
						  << ", loaded .sasset files: " << bootstrap.Stats.SassetsLoaded << ".\n";
				for (const auto& failure : bootstrap.Errors)
				{
					std::cerr << "[Assets] [" << DevelopmentAssetErrorStageName(failure.Stage) << "] " << failure.SourcePath.string()
							  << ": " << failure.Message << '\n';
				}
			}
		}
#endif

		inputSystem = std::make_unique<Swim::Input::InputSystem>();
		inputSystem->Reset();
		commandRegistry = std::make_unique<Swim::Commands::CommandRegistry>();

		switch (physicsBackend)
		{
		case PhysicsBackend::PhysX:
#if SWIM_ENABLE_PHYSX_BACKEND
			physicsSystem = std::make_unique<PhysicsSystem>(CreatePhysXBackend());
			break;
#else
			return -1;
#endif
		case PhysicsBackend::Jolt:
#if SWIM_ENABLE_JOLT_BACKEND
			physicsSystem = std::make_unique<PhysicsSystem>(CreateJoltBackend());
			break;
#else
			return -1;
#endif
		default:
			return -1;
		}
		physicsSystem->SetFixedDeltaSeconds(static_cast<float>(clock.GetFixedDelta()));
		if (const int result = physicsSystem->Awake(); result != 0)
		{
			return ReportLifecycleFailure("Awake", "PhysicsSystem", result);
		}
		if (const int result = physicsSystem->Init(); result != 0)
		{
			return ReportLifecycleFailure("Init", "PhysicsSystem", result);
		}

		if (config.Render)
		{
			if (const int result = InitRenderer(); result != 0)
			{
				return result;
			}
		}
		else
		{
			// No GPU: the UI runtime still runs (documents, input, layout), nothing draws.
			try
			{
				uiRuntime = std::make_unique<UiRuntime>(FindResourceRoot());
				renderServices.Ui = uiRuntime.get();
			}
			catch (const std::exception& error)
			{
				std::cerr << "[Engine] UI runtime unavailable: " << error.what() << '\n';
			}
		}

		// Scenes consume every service above, so they come last.
		SceneServices services;
		services.Files = &platformSystem->GetFileSystem();
		services.Jobs = jobSystem.get();
		services.IO = ioSystem.get();
		services.Assets = assetSystem.get();
		services.FrameMemory = &frameArena;
		services.State = &stateMachine;
		services.Time = &currentFrame;
		services.Clock = &clock;
		services.Tags = &tagRegistry;
		services.Physics = physicsSystem.get();
		services.Input = inputSystem.get();
		services.Camera = cameraSystem.get();
		services.Render = &renderServices;
		services.Commands = commandRegistry.get();
		services.DispatchCommand = [commands = commandRegistry.get()](std::string_view command)
		{
			return commands && commands->ParseAndDispatch(command);
		};
		services.GetFPS = [this]()
		{
			return GetFPS();
		};
		sceneSystem->SetServices(std::move(services));
		if (!config.StartupScene.empty())
		{
			sceneSystem->SetStartupScene(config.StartupScene);
		}

		// The initial state is set before anyone listens: scenes start in it (no transition).
		stateMachine.Set(config.InitialState);
		clock.SetPaused(!stateMachine.IsPlaying());
		stateMachine.Subscribe(
			[this](EngineState previous, EngineState current)
			{
				clock.SetPaused(current != EngineState::Playing);
				if (current == EngineState::Stopped)
				{
					clock.ResetAccumulator();
				}
				sceneSystem->OnEngineStateChanged(previous, current);
				std::cout << "[Engine] State " << ToString(previous) << " -> " << ToString(current) << '\n';
			});

		if (const int result = sceneSystem->Awake(); result != 0)
		{
			return ReportLifecycleFailure("Awake", "SceneSystem", result);
		}
		if (const int result = sceneSystem->Init(); result != 0)
		{
			return ReportLifecycleFailure("Init", "SceneSystem", result);
		}

		if (engineWindow)
		{
			Swim::Platform::WindowEvent initialWindowEvent{};
			initialWindowEvent.Window = engineWindow->GetId();
			initialWindowEvent.LogicalSize = engineWindow->GetLogicalSize();
			initialWindowEvent.PixelSize = engineWindow->GetPixelSize();
			initialWindowEvent.DpiScale = engineWindow->GetDpiScale();
			initialWindowEvent.Type =
				engineWindow->IsFocused() ? Swim::Platform::WindowEventType::FocusGained : Swim::Platform::WindowEventType::FocusLost;
			inputSystem->ProcessWindowEvent(initialWindowEvent);
		}

		RegisterEngineCommands();
		for (const auto& command : config.StartupCommands)
		{
			if (!commandRegistry->ParseAndDispatch(command))
			{
				std::cerr << "[Engine] Unknown startup command: " << command << '\n';
			}
		}
		return 0;
	}

	int SwimEngine::InitRenderer()
	{
		try
		{
			RenderDeviceDesc deviceDesc;
			deviceDesc.Window = engineWindow.get();
			deviceDesc.Width = surfaceWidth;
			deviceDesc.Height = surfaceHeight;
			deviceDesc.VSync = config.VSync;
			deviceDesc.Validation = config.Validation;
			renderDevice = std::make_unique<RenderDevice>(deviceDesc);
			std::cout << "[Render] " << renderDevice->GetAdapterInfo().Name << " (" << renderDevice->GetAdapterInfo().DriverName << "), "
					  << (renderDevice->IsHeadless() ? "headless" : "swapchain") << ", validation "
					  << (renderDevice->IsValidationEnabled() ? "on" : "off") << '\n';

			FrameRendererDesc rendererDesc;
			const auto& files = platformSystem->GetFileSystem();
			const std::array<std::filesystem::path, 1> fallbacks{
#if defined(SWIM_RUNTIME_SHADER_DIR)
				std::filesystem::path(SWIM_RUNTIME_SHADER_DIR)
#else
				files.GetExecutableDirectory() / "Shaders" / "Runtime"
#endif
			};
			rendererDesc.ShaderRoot = ShaderLibrary::FindRoot(files.GetExecutableDirectory(), fallbacks);
			if (rendererDesc.ShaderRoot.empty())
			{
				std::cerr << "[Render] No runtime shaders found next to the executable (Shaders/Runtime). Build the SwimEngine target, or "
							 "set SWIM_SHADER_DIR.\n";
				return -1;
			}
			frameRenderer = std::make_unique<FrameRenderer>(*renderDevice, *assetSystem, *ioSystem, jobSystem.get(), rendererDesc);
			renderBridge = std::make_unique<SceneRenderBridge>(*frameRenderer);
			uiRuntime = std::make_unique<UiRuntime>(FindResourceRoot());
		}
		catch (const std::exception& error)
		{
			std::cerr << "[Render] Renderer initialization failed: " << error.what() << '\n';
			return -1;
		}

		renderServices.Renderer = frameRenderer.get();
		renderServices.Meshes = &frameRenderer->GetMeshes();
		renderServices.Materials = &frameRenderer->GetMaterials();
		renderServices.Settings = &frameRenderer->GetSettings();
		renderServices.Stats = &frameRenderer->GetStats();
		renderServices.Bridge = renderBridge.get();
		renderServices.Ui = uiRuntime.get();
		return 0;
	}

	void SwimEngine::RegisterEngineCommands()
	{
		auto& commands = *commandRegistry;
		commands.Register("play",
			[this](const std::vector<std::string>&)
			{
				stateMachine.Play();
			});
		commands.Register("pause",
			[this](const std::vector<std::string>&)
			{
				stateMachine.Pause();
			});
		commands.Register("resume",
			[this](const std::vector<std::string>&)
			{
				stateMachine.Resume();
			});
		commands.Register("stop",
			[this](const std::vector<std::string>&)
			{
				stateMachine.Stop();
			});
		commands.Register("step",
			[this](const std::vector<std::string>& arguments)
			{
				std::uint32_t steps = 1;
				if (!arguments.empty())
				{
					try
					{
						steps = static_cast<std::uint32_t>(std::max(1, std::stoi(arguments.front())));
					}
					catch (...)
					{
					}
				}
				if (stateMachine.IsPlaying())
				{
					stateMachine.Pause();
				}
				clock.Step(steps);
			});
		commands.Register("timescale",
			[this](const std::vector<std::string>& arguments)
			{
				if (!arguments.empty())
				{
					try
					{
						clock.SetTimeScale(std::stod(arguments.front()));
					}
					catch (...)
					{
					}
				}
			});
		commands.Register("scene",
			[this](const std::vector<std::string>& arguments)
			{
				if (!arguments.empty())
				{
					sceneSystem->RequestScene(arguments.front());
				}
			});
		commands.Register("reload",
			[this](const std::vector<std::string>&)
			{
				sceneSystem->RequestReload();
			});
		commands.Register("capture",
			[this](const std::vector<std::string>& arguments)
			{
				RequestCapture(arguments.empty() ? std::filesystem::path("capture.ppm") : std::filesystem::path(arguments.front()));
			});
		commands.Register("camera",
			[this](const std::vector<std::string>& arguments)
			{
				// camera <eye x y z> [<target x y z>]
				std::array<float, 6> values{ 0, 0, 0, 0, 0, 0 };
				try
				{
					for (std::size_t i = 0; i < std::min<std::size_t>(arguments.size(), values.size()); ++i)
					{
						values[i] = std::stof(arguments[i]);
					}
				}
				catch (...)
				{
					return;
				}
				auto& camera = cameraSystem->GetCamera();
				const glm::vec3 eye{ values[0], values[1], values[2] };
				if (arguments.size() >= 6)
				{
					camera.LookAt(eye, { values[3], values[4], values[5] });
				}
				else
				{
					camera.SetPosition(eye);
				}
				cameraSystem->RequestCameraCut();
				cameraLocked = true;
			});
		commands.Register("quit",
			[this](const std::vector<std::string>&)
			{
				Quit();
			});
	}

	bool SwimEngine::RequestCapture(std::filesystem::path path)
	{
		if (!frameRenderer)
		{
			return false;
		}
		pendingCapture = std::move(path);
		return true;
	}

	void SwimEngine::HandleWindowEvent(const Swim::Platform::WindowEvent& event)
	{
		if (engineWindow && event.Window != 0 && event.Window != engineWindow->GetId())
		{
			return;
		}
		if (inputSystem)
		{
			inputSystem->ProcessWindowEvent(event);
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
			UpdateSurfaceSize();
			break;
		case WindowEventType::Resized:
		case WindowEventType::PixelSizeChanged:
		case WindowEventType::DpiScaleChanged:
			UpdateSurfaceSize();
			break;
		default:
			break;
		}
	}

	void SwimEngine::UpdateSurfaceSize()
	{
		if (!engineWindow)
		{
			return;
		}
		const Swim::Platform::Extent2D size = engineWindow->GetPixelSize();
		surfaceWidth = size.Width;
		surfaceHeight = size.Height;
		if (cameraSystem && surfaceWidth && surfaceHeight)
		{
			cameraSystem->SetSurfaceSize(surfaceWidth, surfaceHeight);
		}
		if (renderDevice)
		{
			renderDevice->RequestResize(surfaceWidth, surfaceHeight);
		}
	}

	void SwimEngine::ApplyCameraComponents()
	{
		const auto& scene = sceneSystem->GetActiveScene();
		if (!scene)
		{
			return;
		}
		auto& registry = scene->GetRegistry();
		const CameraComponent* best = nullptr;
		entt::entity bestEntity = entt::null;
		for (const auto [entity, component, transform] : registry.view<CameraComponent, Transform>().each())
		{
			(void)transform;
			if (component.Active && (!best || component.Priority > best->Priority))
			{
				best = &component;
				bestEntity = entity;
			}
		}
		if (!best)
		{
			return;
		}
		auto& camera = cameraSystem->GetCamera();
		const auto& transform = registry.get<Transform>(bestEntity);
		camera.SetPosition(transform.GetWorldPosition(registry));
		camera.SetRotation(transform.GetWorldRotation(registry));
		try
		{
			camera.SetFieldOfView(best->FieldOfView);
			camera.SetClipPlanes(best->Near, best->Far);
		}
		catch (const std::invalid_argument&)
		{
		}
	}

	void SwimEngine::UpdateTextInput()
	{
		if (!engineWindow || !uiRuntime)
		{
			return;
		}
		const bool wants = uiRuntime->GetInputFrame().WantsTextInput;
		if (wants == textInputActive)
		{
			return;
		}
		auto& windows = platformSystem->GetWindowSystem();
		if (wants)
		{
			windows.StartTextInput(*engineWindow);
		}
		else
		{
			windows.StopTextInput(*engineWindow);
		}
		textInputActive = wants;
	}

	int SwimEngine::Run()
	{
		while (running && Tick())
		{
		}
		return Exit();
	}

	bool SwimEngine::Tick()
	{
		if (!running)
		{
			return false;
		}
		platformSystem->PumpEvents(
			[this](const Swim::Platform::WindowEvent& event)
			{
				HandleWindowEvent(event);
			},
			[this](const Swim::Platform::InputEvent& event)
			{
				if (!inputSystem || !engineWindow || (event.Window != 0 && event.Window != engineWindow->GetId()))
				{
					return;
				}
				inputSystem->ProcessInputEvent(event);
			});
		if (!running)
		{
			return false;
		}

		// Wall-clock delta (or a fixed one for deterministic runs).
		const auto now = std::chrono::steady_clock::now();
		double realDelta = config.FixedFrameDelta > 0.0 ? config.FixedFrameDelta : 0.0;
		if (config.FixedFrameDelta <= 0.0)
		{
			realDelta = havePreviousTime ? std::chrono::duration<double>(now - previousTime).count() : 1.0 / 60.0;
		}
		previousTime = now;
		havePreviousTime = true;

		inputSystem->AdvanceFrame();
		frameArena.BeginFrame(totalFrames + 1);
		currentFrame = clock.Advance(realDelta);

		Update(realDelta);

		if (jobSystem && jobSystem->IsRunning())
		{
			jobSystem->RunMainThreadJobs();
		}
		if (ioSystem && ioSystem->IsRunning())
		{
			ioSystem->PumpCompletions();
		}

		++totalFrames;
		fpsTimeAccumulator += realDelta;
		++fpsFrameCounter;
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

		if (config.MaxFrames != 0 && totalFrames >= config.MaxFrames)
		{
			running = false;
		}
		return running;
	}

	void SwimEngine::Update(double realDelta)
	{
		// Deferred scene switches and Stop resets apply before anything reads the scene.
		sceneSystem->BeginFrame();
		const auto& activeScene = sceneSystem->GetActiveScene();
		Scene* scene = activeScene.get();

		if (frameRenderer)
		{
			frameRenderer->BeginFrame();
		}

		// Camera aspect and the UI view (canvases route this frame's input before gameplay
		// reads it, so behaviours can see whether the UI took the pointer).
		auto& camera = cameraSystem->GetCamera();
		if (surfaceWidth && surfaceHeight)
		{
			camera.SetAspect(static_cast<float>(surfaceWidth) / static_cast<float>(surfaceHeight));
		}
		UiRuntime::ViewDesc uiView;
		uiView.Camera.View = camera.GetViewRowMajor();
		uiView.Camera.Projection = camera.GetProjectionRowMajor();
		uiView.Camera.ViewportWidth = static_cast<float>(surfaceWidth);
		uiView.Camera.ViewportHeight = static_cast<float>(surfaceHeight);
		uiView.DpiScale = engineWindow ? engineWindow->GetDpiScale() : 1.0f;
		if (uiRuntime)
		{
			uiRuntime->Sync(scene, uiView);
			uiRuntime->ApplyInput(engineWindow ? inputSystem.get() : nullptr, static_cast<float>(realDelta));
			UpdateTextInput();
		}

		// Fixed steps: behaviours, then one physics step each (collision callbacks follow).
		for (std::uint32_t step = 0; step < currentFrame.FixedSteps; ++step)
		{
			FixedUpdate(tickCounter);
			tickCounter = tickCounter % 1000 + 1;
		}
		if (scene && physicsSystem && scene->GetPhysicsWorld())
		{
			scene->UpdatePhysics(*physicsSystem, static_cast<float>(currentFrame.Alpha));
		}

		sceneSystem->Update(currentFrame.ScaledDelta);
		ApplyCameraComponents();

		if (!frameRenderer)
		{
			if (uiRuntime)
			{
				uiRuntime->Finish(static_cast<float>(realDelta));
			}
			return;
		}
		if (renderBridge->GetAttached() != scene)
		{
			renderBridge->Attach(scene, sceneSystem->GetActiveSceneId().GetValue());
		}
		renderBridge->Update();

		std::span<const UiDrawItem> ui;
		if (uiRuntime)
		{
			ui = uiRuntime->Finish(static_cast<float>(realDelta));
		}

		RenderFrameInput input;
		auto& render = input.Camera;
		render.View = camera.GetViewRowMajor();
		render.Projection = camera.GetProjectionRowMajor();
		render.Position = ToArray(camera.GetPosition());
		render.Forward = ToArray(camera.GetForward());
		render.Right = ToArray(camera.GetRight());
		render.Up = ToArray(camera.GetUp());
		render.VerticalFov = glm::radians(camera.GetFieldOfView());
		render.Aspect = camera.GetAspect();
		render.Near = camera.GetNearPlane();
		render.Far = camera.GetFarPlane();
		render.Cut = cameraSystem->ConsumeCameraCut();
		input.DeltaTime = static_cast<float>(realDelta);
		input.SimulationDeltaTime = static_cast<float>(clock.GetDelta(SimulationDomain::Particles, currentFrame));
		input.ShadowCasters = renderBridge->GetShadowCasters();
		input.Ui = ui;
		input.GlyphAtlas = uiRuntime ? &uiRuntime->GetAtlas() : nullptr;
		const bool finalFrame = config.MaxFrames != 0 && totalFrames + 1 >= config.MaxFrames;
		input.Capture = !pendingCapture.empty() || (finalFrame && !config.CapturePath.empty());
		try
		{
			if (frameRenderer->Render(input) && input.Capture)
			{
				const auto path = !pendingCapture.empty() ? pendingCapture : std::filesystem::path(config.CapturePath);
				if (frameRenderer->WriteCapture(path))
				{
					std::cout << "[Render] Captured " << frameRenderer->GetCaptureWidth() << "x" << frameRenderer->GetCaptureHeight()
							  << " to " << path.string() << '\n';
				}
				else
				{
					std::cerr << "[Render] Could not write the capture to " << path.string() << '\n';
				}
				pendingCapture.clear();
			}
		}
		catch (const std::exception& error)
		{
			std::cerr << "[Render] Frame failed: " << error.what() << '\n';
			running = false;
		}
	}

	void SwimEngine::FixedUpdate(unsigned int tickThisSecond)
	{
		sceneSystem->FixedUpdate(tickThisSecond);
		const auto& scene = sceneSystem->GetActiveScene();
		if (scene && physicsSystem)
		{
			scene->FixedUpdatePhysics(*physicsSystem, static_cast<float>(currentFrame.FixedDelta));
		}
	}

	int SwimEngine::Exit()
	{
		if (!started)
		{
			return 0;
		}
		started = false;
		running = false;
		int firstError = 0;
		const auto record = [&firstError](std::string_view name, int result)
		{
			if (result != 0)
			{
				ReportLifecycleFailure("Exit", name, result);
				firstError = firstError ? firstError : result;
			}
		};

		// Drain IO first while every consumer and its completion callback target is alive.
		if (ioSystem && ioSystem->IsRunning())
		{
			ioSystem->Shutdown(Swim::IO::IoShutdownMode::Drain);
		}
		if (jobSystem && jobSystem->IsRunning())
		{
			jobSystem->RunMainThreadJobs();
			jobSystem->WaitForAll();
		}

		// Consumers before the services they reference: scenes, then the render bridge
		// (it releases the scenes' GPU objects), the UI, the renderer and the device.
		if (renderDevice)
		{
			renderDevice->WaitIdle();
		}
		if (renderBridge)
		{
			renderBridge->Detach(); // Before the scenes (and their registries) go.
		}
		if (sceneSystem)
		{
			record("SceneSystem", sceneSystem->Exit());
		}
		renderBridge.reset();
		uiRuntime.reset();
		renderServices = {};
		frameRenderer.reset();
		renderDevice.reset();

		if (assetSystem && assetSystem->IsRunning())
		{
			assetSystem->Shutdown();
		}
		assetSystem.reset();

		if (physicsSystem)
		{
			record("PhysicsSystem", physicsSystem->Exit());
		}
		physicsSystem.reset();
		commandRegistry.reset();
		inputSystem.reset();
		ioSystem.reset();
		if (jobSystem)
		{
			jobSystem->Shutdown(Swim::Jobs::JobShutdownMode::Drain);
			jobSystem.reset();
		}

		engineWindow.reset();
		if (platformSystem)
		{
			platformSystem->Shutdown();
		}
		return firstError;
	}
} // namespace Engine
