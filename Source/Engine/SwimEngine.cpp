#include "Engine/SwimEngine.h"
#include "Engine/Runtime/RuntimeConsole.h"
#include "Engine/Runtime/RuntimeConsoleOverlay.h"

#include "Engine/Components/CameraComponent.h"
#include "Engine/Input/InputNames.h"
#include "Engine/Components/Transform.h"
#include "Engine/Runtime/UiRuntime.h"
#include "Engine/Runtime/FramePacing.h"
#include "Engine/Systems/Audio/AudioSystem.h"
#include "Engine/Runtime/RenderQuality.h"
#include "Engine/Runtime/ShaderHotReload.h"
#include "Engine/Runtime/ShaderTweaks.h"
#include "Engine/Runtime/WindowSettings.h"
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
#include <cstdio>
#include <optional>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{

	// Development builds read loose assets (and cook into Assets/Cooked) straight from the
	// repository, so there is one cooked cache and nothing to copy or sync: an explicit
	// --assets wins, then the repository's Assets/ folder (SWIM_DEVELOPMENT_ASSET_ROOT),
	// then <exe dir>/Assets (empty path) for packaged builds.
	std::filesystem::path ResolveAssetRoot(const std::string& requested)
	{
		if (!requested.empty())
		{
			return std::filesystem::path(requested);
		}

#ifdef SWIM_DEVELOPMENT_ASSET_ROOT
		std::error_code error;
		// The literal is UTF-8 (/utf-8 on MSVC): keep non-ASCII repository paths intact.
		const std::string_view literal = SWIM_DEVELOPMENT_ASSET_ROOT;
		const std::filesystem::path development(std::u8string(literal.begin(), literal.end()));

		if (std::filesystem::is_directory(development, error))
		{
			return development;
		}

#endif
		return {};
	}

} // namespace

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
		platformDesc.OrganizationName = config.OrganizationName;
		platformDesc.ApplicationName = config.ApplicationName;
		platformDesc.Headless = config.Present == PresentMode::Headless;
		platformDesc.AssetRoot = ResolveAssetRoot(config.AssetRoot);

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

	std::filesystem::path SwimEngine::FindFontRoot() const
	{
		// The UI fonts live in Assets/Fonts: the resolved asset root (--assets, the
		// repository's Assets/ in development builds, else <exe dir>/Assets) first.
		const auto& files = platformSystem->GetFileSystem();
		const std::array<std::filesystem::path, 3> candidates{ files.GetAssetRoot(), files.GetExecutableDirectory() / "Assets",
			std::filesystem::current_path() / "Assets" };

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
				const auto bootstrapStart = std::chrono::steady_clock::now();
				const auto bootstrap = Swim::AssetCompiler::RunDevelopmentAssetBootstrap(assetRoot, *assetSystem);
				const double bootstrapMs =
					std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - bootstrapStart).count();
				std::cout << "[Assets] Bootstrap took " << static_cast<int>(bootstrapMs) << " ms. Sources: " << bootstrap.Stats.SourcesDiscovered << ", current: " << bootstrap.Stats.SourcesCurrent
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
		actionMap = std::make_unique<Swim::Input::ActionMap>();
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
				uiRuntime = std::make_unique<UiRuntime>(FindFontRoot());
				renderServices.Ui = uiRuntime.get();
				CreateConsole();
			}
			catch (const std::exception& error)
			{
				std::cerr << "[Engine] UI runtime unavailable: " << error.what() << '\n';
			}
		}

		// Profiling switches (render.toggle) over the renderer, its features and what scenes add.
		renderToggles = std::make_unique<RenderToggles>(frameRenderer.get());
		renderServices.Toggles = renderToggles.get();
		renderServices.Profiler = &profiler;

		if (config.Audio != AudioOutput::Off)
		{
			const auto& files = platformSystem->GetFileSystem();
			AudioSystemDesc audioDesc;
			audioDesc.Output = config.Present == PresentMode::Headless ? AudioOutput::Null : config.Audio;
			audioDesc.AssetRoot = files.GetAssetRoot();
			audioSystem = std::make_unique<AudioSystem>(audioDesc, commandRegistry.get());

			// Development sounds outside the repository (DevPaths.cfg DevAudioRoot).
			if (const auto devAudio = files.GetDevPath("DevAudioRoot"); !devAudio.empty())
			{
				audioSystem->MountRoot("devaudio", devAudio);
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
		services.Actions = actionMap.get();
		services.Camera = cameraSystem.get();
		services.Render = &renderServices;
		services.Audio = audioSystem.get();
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
		windowSettings = std::make_unique<WindowSettings>(*commandRegistry, platformSystem->GetWindowSystem(), engineWindow.get(), config.Window);
		framePacing = std::make_unique<FramePacing>(*commandRegistry);
		renderQuality = std::make_unique<RenderQuality>(*commandRegistry);

		if (renderDevice)
		{
			framePacing->SetDevice(renderDevice.get());
			renderServices.Pacing = &framePacing->GetStats();
		}

		if (renderServices.Settings)
		{
			renderQuality->SetSettings(renderServices.Settings);
		}

		if (const auto path = GetUserConfigPath(); !path.empty() && commandRegistry->ExecuteFile(path))
		{
			std::cout << "[Config] Loaded " << path.string() << '\n';
		}

		commandRegistry->MarkConfigSaved();

		for (const auto& command : config.StartupCommands)
		{
			try
			{
				if (!commandRegistry->ParseAndDispatch(command))
				{
					std::cerr << "[Engine] Unknown startup command: " << command << '\n';
				}
			}
			catch (const std::exception& error)
			{
				std::cerr << "[Engine] Startup command failed: " << error.what() << '\n';
			}
		}

		commandRegistry->SetStartupComplete();
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
			deviceDesc.FramesInFlight = config.FramesInFlight;
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
			uiRuntime = std::make_unique<UiRuntime>(FindFontRoot());
		}
		catch (const std::exception& error)
		{
			std::cerr << "[Render] Renderer initialization failed: " << error.what() << '\n';
			return -1;
		}

		if (!shaderTweaks)
		{
			shaderTweaks = std::make_unique<ShaderTweaks>(*commandRegistry);
		}

		frameRenderer->SetConstantsOverride(
			[tweaks = shaderTweaks.get()](std::string_view program, const RuntimeComputeProgram& compiled, std::span<std::byte> constants)
			{
				tweaks->Apply(program, compiled, constants);
			});

		// Development: recompile changed runtime shaders (DevPaths.cfg names the compiler and sources).
		if (const auto& files = platformSystem->GetFileSystem(); !files.GetDevPath("SlangCompiler").empty())
		{
			ShaderHotReloadDesc reloadDesc;
			reloadDesc.Compiler = files.GetDevPath("SlangCompiler");
			reloadDesc.Sources = files.GetDevPath("ShaderSources");
			reloadDesc.CacheRoot = files.GetCacheRoot() / "Shaders";
			shaderHotReload = std::make_unique<ShaderHotReload>(reloadDesc, *commandRegistry, *frameRenderer);
		}

		renderServices.Renderer = frameRenderer.get();
		renderServices.Meshes = &frameRenderer->GetMeshes();
		renderServices.Materials = &frameRenderer->GetMaterials();
		renderServices.Settings = &frameRenderer->GetSettings();

		if (const auto* budget = commandRegistry ? commandRegistry->FindVariable("render_upload_budget_mb") : nullptr)
		{
			renderServices.Settings->StreamingUploadBudget = std::uint64_t(budget->GetInt()) << 20;
		}
		renderServices.Stats = &frameRenderer->GetStats();

		if (renderQuality)
		{
			renderQuality->SetSettings(renderServices.Settings);
		}

		if (framePacing)
		{
			framePacing->SetDevice(renderDevice.get());
			renderServices.Pacing = &framePacing->GetStats();
		}

		renderServices.Bridge = renderBridge.get();
		renderServices.Ui = uiRuntime.get();
		CreateConsole();
		return 0;
	}

	void SwimEngine::PrintRenderStats() const
	{
		if (!frameRenderer)
		{
			std::cout << "[Render] no renderer\n";
			return;
		}

		const auto& s = frameRenderer->GetStats();
		std::cout << "[Render] frame " << s.Frame << ": " << s.Width << "x" << s.Height << ", CPU " << s.CpuMilliseconds << " ms, GPU "
				  << (s.GpuTimingsAvailable ? std::to_string(s.GpuMilliseconds) + " ms" : std::string("n/a")) << " (" << s.Passes
				  << " passes), probes " << s.ReflectionProbes << " active / " << s.ReflectionProbeFaces << " faces captured\n";

		for (std::uint32_t i = 0; i < s.TopPassCount; ++i)
		{
			std::cout << "[Render]   " << s.TopPasses[i].Name << ": " << s.TopPasses[i].Milliseconds << " ms\n";
		}
	}

	void SwimEngine::CreateConsole()
	{
		if (!uiRuntime || !commandRegistry || consoleOverlay)
		{
			return;
		}

		console = std::make_unique<RuntimeConsole>(*commandRegistry);
		consoleOverlay = std::make_unique<RuntimeConsoleOverlay>(*uiRuntime, *console);
	}

	void SwimEngine::RegisterEngineCommands()
	{
		auto& commands = *commandRegistry;
		commands
			.AddInt("render_upload_budget_mb", 16, 1, 1024, "Megabytes streaming buffers upload per frame", "Video/Rendering",
				Swim::Commands::VariableFlags::Saved)
			.Subscribe(
				[this](const Swim::Commands::ConsoleVariable& variable)
				{
					if (renderServices.Settings)
					{
						renderServices.Settings->StreamingUploadBudget = std::uint64_t(variable.GetInt()) << 20;
					}
				});
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
		// capture.sequence <prefix> <after frames> <count>: captures `count` consecutive frames,
		// starting `after frames` from now, as <prefix>_000.ppm, <prefix>_001.ppm, ... (temporal
		// artefacts - ghosting, jitter - are only visible frame to frame).
		commands.Register("capture.sequence",
			[this](const std::vector<std::string>& arguments)
			{
				if (arguments.size() < 3)
				{
					throw std::invalid_argument("usage: capture.sequence <prefix> <after frames> <count>");
				}

				captureSequencePrefix = arguments[0];
				captureSequenceStart = totalFrames + static_cast<std::uint64_t>(std::max(0, std::stoi(arguments[1])));
				captureSequenceCount = static_cast<std::uint32_t>(std::max(0, std::stoi(arguments[2])));
				captureSequenceDone = 0;
			});
		// after <frames> <command...>: runs a console line that many frames from now (time
		// freezes and resumes, toggles mid-sequence: "timescale 0" then "after 40 timescale 1"
		// holds a moving object in place while the reflections settle).
		commands.Register("after",
			[this](const std::vector<std::string>& arguments)
			{
				if (arguments.size() < 2)
				{
					throw std::invalid_argument("usage: after <frames> <command...>");
				}

				std::string line;

				for (std::size_t i = 1; i < arguments.size(); ++i)
				{
					line += (i > 1 ? " " : "") + arguments[i];
				}

				deferredCommands.push_back({ totalFrames + static_cast<std::uint64_t>(std::max(0, std::stoi(arguments[0]))), line });
			});
		// camera.cut: drop temporal history on the next frame (as a teleport would).
		commands.Register("camera.cut",
			[this](const std::vector<std::string>&)
			{
				cameraSystem->RequestCameraCut();
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
		// render.stats: the last frame's CPU/GPU times, the costliest GPU passes and the
		// reflection probe work; render.stats <n>: print it every n frames (0 stops).
		commands.Register("render.stats",
			[this](const std::vector<std::string>& arguments)
			{
				if (!arguments.empty())
				{
					try
					{
						statsInterval = static_cast<std::uint32_t>(std::max(0, std::stoi(arguments[0])));
					}
					catch (...)
					{
						statsInterval = 0;
					}

					return;
				}

				PrintRenderStats();
			});
		// profile <frames> [warmup] [csv path] [label] [quit]: measures every CPU zone, renderer
		// phase and GPU pass over <frames> frames (after [warmup]) and prints the costliest;
		// with a path, appends the full table to that CSV; "quit" ends the run after it.
		commands.Register("profile",
			[this](const std::vector<std::string>& arguments)
			{
				const auto number = [&](std::size_t i, std::uint32_t fallback)
				{
					try
					{
						return i < arguments.size() ? static_cast<std::uint32_t>(std::max(0, std::stoi(arguments[i]))) : fallback;
					}
					catch (...)
					{
						return fallback;
					}
				};
				const std::uint32_t frames = number(0, 240);
				const std::uint32_t warmup = number(1, 60);
				const std::filesystem::path csv = arguments.size() > 2 && arguments[2] != "-" ? std::filesystem::path(arguments[2]) : std::filesystem::path();
				const std::string label = arguments.size() > 3 ? arguments[3] : std::string();
				quitAfterProfile = arguments.size() > 4 && arguments[4] == "quit";
				profiler.Begin(warmup, frames, csv, label);
				std::cout << "[Profile] capturing " << frames << " frames after " << warmup << " warm-up frames\n";
			});
		// render.toggles [filter]: every profiling switch and its state; render.toggle
		// <name|group.*|all> [0|1]: set (or flip) switches.
		commands.Register("render.toggles",
			[this](const std::vector<std::string>& arguments)
			{
				if (!renderToggles)
				{
					return;
				}

				const std::string filter = arguments.empty() ? std::string() : arguments[0];

				for (const auto& toggle : renderToggles->List())
				{
					if (!filter.empty() && toggle.Name.find(filter) == std::string::npos)
					{
						continue;
					}

					std::cout << "[Toggle] " << (toggle.Get() ? "on  " : "off ") << toggle.Name << "  - " << toggle.Description << '\n';
				}

			});
		commands.Register("render.toggle",
			[this](const std::vector<std::string>& arguments)
			{
				if (!renderToggles || arguments.empty())
				{
					throw std::invalid_argument("usage: render.toggle <name|group.*|all> [0|1]");
				}

				bool on = true;

				if (arguments.size() > 1)
				{
					on = arguments[1] != "0" && arguments[1] != "off" && arguments[1] != "false";
				}
				else if (const auto current = renderToggles->Get(arguments[0]))
				{
					on = !*current;
				}

				const auto changed = renderToggles->Set(arguments[0], on);

				if (changed == 0)
				{
					throw std::invalid_argument("no render toggle matches " + arguments[0] + " (see render.toggles)");
				}

				std::cout << "[Toggle] " << arguments[0] << " -> " << (on ? "on" : "off") << " (" << changed << ")\n";
			});
		// render.set [<knob> <value>]: the numeric quality knobs of the render settings, live
		// (bench sweeps them: "tile32=render.set cluster.tile 32"). No arguments: lists them.
		commands.Register("render.set",
			[this](const std::vector<std::string>& arguments)
			{
				if (!renderServices.Settings)
				{
					throw std::invalid_argument("render.set needs the renderer");
				}

				auto& s = *renderServices.Settings;
				auto& ssr = s.ScreenSpace.Reflections;
				auto& ao = s.ScreenSpace.AmbientOcclusion;
				auto& shadow = s.Shadow;
				auto& probes = s.ReflectionProbes;
				auto& planar = s.PlanarReflections;
				std::uint32_t ssrHalf = ssr.HalfResolution ? 1u : 0u;
				std::uint32_t aoHalf = ao.HalfResolution ? 1u : 0u;
				struct Knob
				{
					const char* Name;
					float* F = nullptr;
					std::uint32_t* U = nullptr;
				};
				const Knob knobs[] = {
					{ "cluster.tile", nullptr, &s.ClusterTileSize },
					{ "cluster.slices", nullptr, &s.ClusterSlices },
					{ "cluster.far", &s.ClusterFar },
					{ "ssr.steps", nullptr, &ssr.MaxSteps },
					{ "ssr.stride", &ssr.Stride },
					{ "ssr.refine", nullptr, &ssr.RefineSteps },
					{ "ssr.distance", &ssr.MaxDistance },
					{ "ssr.roughness", &ssr.MaxRoughness },
					{ "ssr.half", nullptr, &ssrHalf },
					{ "ao.half", nullptr, &aoHalf },
					{ "reflections.blend", &ssr.TemporalBlend },
					{ "ao.slices", nullptr, &ao.SliceCount },
					{ "ao.steps", nullptr, &ao.StepCount },
					{ "ao.radius", &ao.Radius },
					{ "shadow.cascade", nullptr, &shadow.CascadeResolution },
					{ "shadow.spot", nullptr, &shadow.SpotResolution },
					{ "shadow.point", nullptr, &shadow.PointResolution },
					{ "shadow.spots", nullptr, &shadow.MaxSpotShadows },
					{ "shadow.points", nullptr, &shadow.MaxPointShadows },
					{ "shadow.pcf", nullptr, &shadow.PcfRadius },
					{ "probes.resolution", nullptr, &probes.Resolution },
					{ "probes.faces", nullptr, &probes.FacesPerFrame },
					{ "probes.filters", nullptr, &probes.FiltersPerFrame },
					{ "probes.idle", nullptr, &probes.IdleRefreshFrames },
					{ "probes.samples", nullptr, &probes.PrefilterSamples },
					{ "probes.cull", &probes.CullDistance },
					{ "planar.planes", nullptr, &planar.MaxPlanes },
					{ "planar.atlas", nullptr, &planar.AtlasResolution },
					{ "planar.captures", nullptr, &planar.CapturesPerFrame },
					{ "planar.supersample", nullptr, &planar.Supersample },
					{ "planar.scale", &planar.ResolutionScale },
					{ "planar.min-screen", &planar.MinScreenFraction },
					{ "planar.ssr-fallback", &planar.SsrFallbackScreenFraction },
					{ "planar.sphere-cos", &planar.SphereMinCosine },
					{ "planar.motion", &planar.MotionTolerance },
					{ "planar.max-age", &planar.MaxAgeSeconds },
					{ "planar.cull", &planar.CullDistance },
				};

				if (arguments.size() < 2)
				{
					for (const auto& knob : knobs)
					{
						std::cout << "[Set] " << knob.Name << " = " << (knob.F ? *knob.F : static_cast<float>(*knob.U)) << "\n";
					}

					return;
				}

				for (const auto& knob : knobs)
				{
					if (arguments[0] == knob.Name)
					{
						const float value = std::stof(arguments[1]);

						if (knob.F)
						{
							*knob.F = value;
						}
						else
						{
							*knob.U = static_cast<std::uint32_t>(std::max(0.0f, value));
						}

						ssr.HalfResolution = ssrHalf != 0u;
						ao.HalfResolution = aoHalf != 0u;
						std::cout << "[Set] " << knob.Name << " -> " << value << "\n";
						return;
					}
				}

				throw std::invalid_argument("unknown render knob " + arguments[0] + " (render.set lists them)");
			});
		// bench <csv> <frames> <warmup> <ablate|base> <scenario>[|<scenario>...] [quit]
		// A scenario is "<label>=<command>[;<command>...]" (e.g. "atrium=sandbox.view 5").
		// For each scenario: its commands, a baseline capture, and with "ablate" one capture
		// per profiling switch turned off alone (and back on after). Captures append to the CSV
		// labelled "<scenario>|baseline" or "<scenario>|-<switch>".
		commands.Register("bench",
			[this](const std::vector<std::string>& arguments)
			{
				if (arguments.size() < 5)
				{
					throw std::invalid_argument("usage: bench <csv> <frames> <warmup> <ablate|base> <label=cmd;cmd|label=cmd> [quit]");
				}

				benchCsv = arguments[0];
				benchFrames = static_cast<std::uint32_t>(std::max(1, std::stoi(arguments[1])));
				benchWarmup = static_cast<std::uint32_t>(std::max(0, std::stoi(arguments[2])));
				const bool ablate = arguments[3] == "ablate";
				// The scenario list may have been split on spaces: join the rest.
				std::string scenarios;
				benchQuit = false;

				for (std::size_t i = 4; i < arguments.size(); ++i)
				{
					if (i + 1 == arguments.size() && arguments[i] == "quit")
					{
						benchQuit = true;
						break;
					}

					scenarios += (scenarios.empty() ? "" : " ") + arguments[i];
				}

				benchSteps.clear();
				benchNext = 0;
				std::size_t start = 0;

				while (start <= scenarios.size())
				{
					const auto end = std::min(scenarios.find('|', start), scenarios.size());
					const std::string scenario = scenarios.substr(start, end - start);
					start = end + 1;

					if (scenario.empty())
					{
						continue;
					}

					const auto equals = scenario.find('=');
					const std::string label = equals == std::string::npos ? scenario : scenario.substr(0, equals);
					std::string setup = equals == std::string::npos ? std::string() : scenario.substr(equals + 1);
					// Commands separated by ';' run one after another.
					std::size_t from = 0;

					while (from <= setup.size())
					{
						const auto to = std::min(setup.find(';', from), setup.size());
						benchSteps.push_back({ setup.substr(from, to - from), "", "" });
						from = to + 1;
					}

					benchSteps.push_back({ "", label + "|baseline", "" });

					if (ablate && renderToggles)
					{
						for (const auto& toggle : renderToggles->List())
						{
							if (!toggle.Get())
							{
								continue; // Already off in this scenario.
							}

							benchSteps.push_back({ "render.toggle " + toggle.Name + " 0", label + "|-" + toggle.Name,
								"render.toggle " + toggle.Name + " 1" });
						}
					}
				}

				std::cout << "[Bench] " << benchSteps.size() << " steps\n";
			});
		commands.Register("quit",
			[this](const std::vector<std::string>&)
			{
				Quit();
			});
		RegisterInputCommands();
		commands.Register({ "exec", "Runs every line of a config file", "Console", "<file>" },
			[this](const std::vector<std::string>& arguments)
			{
				if (arguments.empty())
				{
					throw std::invalid_argument("usage: exec <file>");
				}

				std::filesystem::path path(arguments[0]);

				if (path.is_relative() && !std::filesystem::exists(path))
				{
					path = platformSystem->GetFileSystem().GetUserDataRoot() / path;
				}

				if (!commandRegistry->ExecuteFile(path))
				{
					throw std::invalid_argument("cannot read " + path.string());
				}
			});
		commands.Register({ "config_save", "Writes the saved settings to the user config file now", "Console", "[file]" },
			[this](const std::vector<std::string>& arguments)
			{
				const auto path = arguments.empty() ? GetUserConfigPath() : std::filesystem::path(arguments[0]);

				if (path.empty() || !commandRegistry->SaveConfig(path))
				{
					throw std::invalid_argument("could not write the config file");
				}

				if (arguments.empty())
				{
					commandRegistry->MarkConfigSaved();
				}

				std::cout << "[Config] Saved " << path.string() << '\n';
			});
		commands.Register({ "cvar_list", "Lists console variables and their values", "Console", "[filter]" },
			[this](const std::vector<std::string>& arguments)
			{
				const std::string filter = arguments.empty() ? std::string() : arguments[0];

				for (const auto* variable : commandRegistry->GetVariables())
				{
					const auto& desc = variable->GetDesc();

					if (!filter.empty() && variable->GetName().find(filter) == std::string::npos &&
						desc.Category.find(filter) == std::string::npos)
					{
						continue;
					}

					std::cout << variable->GetName() << " = " << Swim::Commands::CommandRegistry::QuoteArgument(variable->ToString())
							  << (variable->IsDefault() ? "" : " *") << (variable->IsRestartPending() ? " (restart)" : "") << "  ["
							  << desc.Category << "] " << desc.Description << '\n';
				}
			});
		commands.Register({ "cvar_reset", "Resets a console variable (or all) to its default", "Console", "<name|all>" },
			[this](const std::vector<std::string>& arguments)
			{
				if (arguments.empty())
				{
					throw std::invalid_argument("usage: cvar_reset <name|all>");
				}

				if (arguments[0] == "all")
				{
					for (auto* variable : commandRegistry->GetVariables())
					{
						if (!Swim::Commands::HasFlag(variable->GetDesc().Flags, Swim::Commands::VariableFlags::ReadOnly))
						{
							variable->Reset();
						}
					}

					return;
				}

				auto* variable = commandRegistry->FindVariable(arguments[0]);

				if (!variable)
				{
					throw std::invalid_argument("no console variable " + arguments[0]);
				}

				variable->Reset();
			});
	}

	void SwimEngine::RegisterInputCommands()
	{
		using Swim::Commands::VariableFlags;
		auto& commands = *commandRegistry;
		auto& actions = *actionMap;
		actions.SetChangedCallback(
			[this]()
			{
				commandRegistry->MarkConfigDirty();
			});
		commands.SetConfigWriter("bindings",
			[this](std::string& out)
			{
				actionMap->WriteConfig(out);
			});

		auto& sensitivity = commands.AddFloat("mouse_sensitivity", 1.0, 0.01, 50.0, "Multiplier on raw mouse motion for looking",
			"Controls/Mouse", VariableFlags::Saved);
		auto& invert = commands.AddBool("mouse_invert_y", false, "Inverts vertical mouse look", "Controls/Mouse", VariableFlags::Saved);
		actions.SetMouseSensitivity(sensitivity.GetFloat());
		actions.SetInvertMouseY(invert.GetBool());
		sensitivity.Subscribe(
			[this](const Swim::Commands::ConsoleVariable& variable)
			{
				actionMap->SetMouseSensitivity(variable.GetFloat());
			});
		invert.Subscribe(
			[this](const Swim::Commands::ConsoleVariable& variable)
			{
				actionMap->SetInvertMouseY(variable.GetBool());
			});

		Swim::Input::ActionDesc fullscreen;
		fullscreen.Name = "toggle_fullscreen";
		fullscreen.Description = "Switches between windowed and fullscreen";
		fullscreen.Category = "Controls/Window";
		fullscreen.Command = "fullscreen_toggle";
		fullscreen.DefaultBindings.push_back({});
		fullscreen.DefaultBindings.back().Type = Swim::Input::InputBindingType::ScanCode;
		fullscreen.DefaultBindings.back().PhysicalKey = Swim::Platform::ScanCode::F11;
		actions.RegisterAction(std::move(fullscreen));

		commands.Register({ "bind", "Shows or replaces the inputs of an action", "Controls", "<action> [none | <input>...]" },
			[this](const std::vector<std::string>& arguments)
			{
				actionMap->ExecuteBind(arguments);
			});
		commands.Register({ "bind_add", "Adds an input to an action", "Controls", "<action> <input>" },
			[this](const std::vector<std::string>& arguments)
			{
				Swim::Input::InputBinding binding;
				const auto action = arguments.size() == 2 ? actionMap->FindAction(arguments[0]) : Swim::Input::InvalidAction;

				if (action == Swim::Input::InvalidAction || !Swim::Input::ParseBinding(arguments[1], binding))
				{
					throw std::invalid_argument("usage: bind_add <action> <input>");
				}

				actionMap->AddBinding(action, binding);
			});
		commands.Register({ "unbind", "Removes one input (or all inputs) from an action", "Controls", "<action> [input]" },
			[this](const std::vector<std::string>& arguments)
			{
				const auto action = arguments.empty() ? Swim::Input::InvalidAction : actionMap->FindAction(arguments[0]);

				if (action == Swim::Input::InvalidAction)
				{
					throw std::invalid_argument("usage: unbind <action> [input]");
				}

				Swim::Input::InputBinding binding;

				if (arguments.size() < 2)
				{
					actionMap->SetBindings(action, {});
				}
				else if (Swim::Input::ParseBinding(arguments[1], binding))
				{
					actionMap->RemoveBinding(action, binding);
				}
				else
				{
					throw std::invalid_argument("unknown input " + arguments[1]);
				}
			});
		commands.Register({ "bind_list", "Lists actions and their inputs", "Controls", "[filter]" },
			[this](const std::vector<std::string>& arguments)
			{
				for (Swim::Input::ActionId id = 0; id < actionMap->GetActionCount(); ++id)
				{
					const auto& desc = actionMap->GetDesc(id);

					if (!arguments.empty() && desc.Name.find(arguments[0]) == std::string::npos &&
						desc.Category.find(arguments[0]) == std::string::npos)
					{
						continue;
					}

					std::string line = desc.Name + " =";

					for (const auto& binding : actionMap->GetBindings(id))
					{
						line += " " + Swim::Input::FormatBinding(binding);
					}

					std::cout << line << (actionMap->HasDefaultBindings(id) ? "" : " *") << "  [" << desc.Category << "] " << desc.Description
							  << '\n';
				}
			});
		commands.Register({ "bind_reset", "Restores the default inputs of an action (or all)", "Controls", "<action|all>" },
			[this](const std::vector<std::string>& arguments)
			{
				if (arguments.empty())
				{
					throw std::invalid_argument("usage: bind_reset <action|all>");
				}

				if (arguments[0] == "all")
				{
					actionMap->ResetAllBindings();
					return;
				}

				const auto action = actionMap->FindAction(arguments[0]);

				if (action == Swim::Input::InvalidAction)
				{
					throw std::invalid_argument("no input action " + arguments[0]);
				}

				actionMap->ResetBindings(action);
			});
	}

	std::filesystem::path SwimEngine::GetUserConfigPath() const
	{
		if (config.UserConfigFile.empty() || !platformSystem)
		{
			return {};
		}

		const std::filesystem::path file(config.UserConfigFile);

		if (file.is_absolute())
		{
			return file;
		}

		const auto& root = platformSystem->GetFileSystem().GetUserDataRoot();
		return root.empty() ? std::filesystem::path() : root / file;
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
		case WindowEventType::ModeChanged:
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

		const auto tickStart = std::chrono::steady_clock::now();

		if (haveLastTick)
		{
			// The whole previous frame, start to start (what the frame rate is).
			profiler.Add("frame", "Frame (wall, start to start)", std::chrono::duration<double, std::milli>(tickStart - lastTickStart).count());
		}

		lastTickStart = tickStart;
		haveLastTick = true;

		if (profiler.EndFrame())
		{
			const auto& report = profiler.GetReport();
			std::cout << FrameProfiler::Summary(report, benchSteps.empty() ? 12 : 4);

			if (quitAfterProfile)
			{
				running = false;
				return false;
			}
		}

		// Deferred console lines (the `after` command) whose frame has come, in order.
		for (std::size_t i = 0; i < deferredCommands.size();)
		{
			if (deferredCommands[i].first > totalFrames)
			{
				++i;
				continue;
			}

			const std::string line = deferredCommands[i].second;
			deferredCommands.erase(deferredCommands.begin() + static_cast<std::ptrdiff_t>(i));

			try
			{
				commandRegistry->ParseAndDispatch(line);
			}
			catch (const std::exception& error)
			{
				std::cerr << "[Console] after: " << error.what() << '\n';
			}
		}

		if (!benchSteps.empty() && !profiler.IsCapturing())
		{
			AdvanceBench();

			if (!running)
			{
				return false;
			}
		}

		if (framePacing)
		{
			// Frame cap and GPU wait before input, so input is sampled as late as possible.
			FrameProfiler::Scope pacingScope(profiler, "Frame pacing");
			framePacing->BeginFrame();
		}

		std::optional<FrameProfiler::Scope> eventsScope;
		eventsScope.emplace(profiler, "Platform events");
		platformSystem->PumpEvents(
			[this](const Swim::Platform::WindowEvent& event)
			{
				HandleWindowEvent(event);
			},
			[this](const Swim::Platform::InputEvent& event)
			{
				if (!inputSystem || !engineWindow || !config.PlatformInput || (event.Window != 0 && event.Window != engineWindow->GetId()))
				{
					return;
				}

				inputSystem->ProcessInputEvent(event);
			});
		eventsScope.reset();

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
		actionMap->SetContextActive(actionMap->GetTextContext(), textInputActive || (console && console->IsOpen()));
		actionMap->Update(*inputSystem,
			[this](std::string_view line)
			{
				try
				{
					commandRegistry->ParseAndDispatch(line);
				}
				catch (const std::exception& error)
				{
					std::cerr << "[Input] " << line << ": " << error.what() << '\n';
				}
			});
		frameArena.BeginFrame(totalFrames + 1);
		currentFrame = clock.Advance(realDelta);

		Update(realDelta);

		{
			FrameProfiler::Scope scope(profiler, "Jobs and IO completions");

			if (jobSystem && jobSystem->IsRunning())
			{
				jobSystem->RunMainThreadJobs();
			}

			if (ioSystem && ioSystem->IsRunning())
			{
				ioSystem->PumpCompletions();
			}
		}
		profiler.Add("frame", "Tick (CPU, excluding the wait for the next frame)",
			std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tickStart).count());

		++totalFrames;

		if (statsInterval != 0 && totalFrames % statsInterval == 0)
		{
			PrintRenderStats();
		}

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
		std::optional<FrameProfiler::Scope> zone;
		zone.emplace(profiler, "Scene begin frame");
		sceneSystem->BeginFrame();
		const auto& activeScene = sceneSystem->GetActiveScene();
		Scene* scene = activeScene.get();

		zone.emplace(profiler, "Renderer begin frame (collect)");

		if (frameRenderer)
		{
			frameRenderer->BeginFrame();
		}

		zone.emplace(profiler, "UI sync and input");

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

			if (consoleOverlay)
			{
				consoleOverlay->BeforeInput(engineWindow ? inputSystem.get() : nullptr);
			}

			uiRuntime->ApplyInput(engineWindow ? inputSystem.get() : nullptr, static_cast<float>(realDelta));

			if (consoleOverlay)
			{
				consoleOverlay->AfterInput();
			}

			UpdateTextInput();
		}

		zone.reset();
		// Fixed steps: behaviours, then one physics step each (collision callbacks follow).
		for (std::uint32_t step = 0; step < currentFrame.FixedSteps; ++step)
		{
			FixedUpdate(tickCounter);
			tickCounter = tickCounter % 1000 + 1;
		}

		zone.emplace(profiler, "Physics interpolation");

		if (scene && physicsSystem && scene->GetPhysicsWorld())
		{
			scene->UpdatePhysics(*physicsSystem, static_cast<float>(currentFrame.Alpha));
		}

		zone.emplace(profiler, "Behaviours (update)");
		sceneSystem->Update(currentFrame.ScaledDelta);
		zone.emplace(profiler, "Camera components");
		ApplyCameraComponents();

		if (audioSystem)
		{
			zone.emplace(profiler, "Audio");
			AudioListener listener;
			listener.Position = camera.GetPosition();
			listener.Forward = camera.GetForward();
			listener.Up = camera.GetUp();
			audioSystem->Update(realDelta, listener);
		}

		if (shaderHotReload)
		{
			shaderHotReload->Update();
		}

		zone.reset();

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

		zone.emplace(profiler, "Render bridge (extraction, lights, probes)");
		renderBridge->Update();

		zone.emplace(profiler, "UI layout and paint");
		std::span<const UiDrawItem> ui;

		if (uiRuntime)
		{
			ui = uiRuntime->Finish(static_cast<float>(realDelta));
		}

		zone.reset();

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
		input.ReflectionProbes = renderBridge->GetReflectionProbes();
		input.ReflectionMovers = renderBridge->GetReflectionMovers();
		input.PlanarReflectors = renderBridge->GetPlanarReflectors();
		input.Ui = ui;
		input.GlyphAtlas = uiRuntime ? &uiRuntime->GetAtlas() : nullptr;

		if (captureSequenceDone < captureSequenceCount && totalFrames >= captureSequenceStart && pendingCapture.empty())
		{
			char name[32];
			std::snprintf(name, sizeof(name), "_%03u.ppm", captureSequenceDone++);
			pendingCapture = captureSequencePrefix + name;
		}

		const bool finalFrame = config.MaxFrames != 0 && totalFrames + 1 >= config.MaxFrames;
		input.Capture = !pendingCapture.empty() || (finalFrame && !config.CapturePath.empty());

		try
		{
			const auto renderStart = std::chrono::steady_clock::now();
			const bool rendered = frameRenderer->Render(input);

			if (framePacing)
			{
				framePacing->EndFrame();
			}

			profiler.Add("cpu", "Render (renderer CPU total)",
				std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - renderStart).count());
			RecordFrameProfile(0.0);

			if (rendered && input.Capture)
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
		{
			FrameProfiler::Scope scope(profiler, "Behaviours (fixed update)");
			sceneSystem->FixedUpdate(tickThisSecond);
		}
		FrameProfiler::Scope scope(profiler, "Physics step");
		const auto& scene = sceneSystem->GetActiveScene();

		if (scene && physicsSystem)
		{
			scene->FixedUpdatePhysics(*physicsSystem, static_cast<float>(currentFrame.FixedDelta));
		}
	}

	void SwimEngine::AdvanceBench()
	{
		const auto run = [this](const std::string& line)
		{
			if (line.empty())
			{
				return;
			}

			try
			{
				commandRegistry->ParseAndDispatch(line);
			}
			catch (const std::exception& error)
			{
				std::cerr << "[Bench] " << line << ": " << error.what() << '\n';
			}
		};
		run(benchRestore);
		benchRestore.clear();

		while (benchNext < benchSteps.size())
		{
			const auto step = benchSteps[benchNext++];
			run(step.Command);

			if (!step.Label.empty())
			{
				benchRestore = step.Restore;
				profiler.Begin(benchWarmup, benchFrames, benchCsv, step.Label);
				std::cout << "[Bench] " << benchNext << "/" << benchSteps.size() << ": " << step.Label << '\n';
				return;
			}
		}

		std::cout << "[Bench] done: " << benchCsv.string() << '\n';
		benchSteps.clear();
		benchNext = 0;

		if (benchQuit)
		{
			running = false;
		}
	}

	void SwimEngine::RecordFrameProfile(double)
	{
		if (!frameRenderer)
		{
			return;
		}

		const auto& stats = frameRenderer->GetStats();

		for (const auto& phase : stats.CpuPhases)
		{
			profiler.Add("render", phase.Name, phase.Milliseconds);
		}

		for (const auto& phase : stats.RecordPhases)
		{
			profiler.Add("render", phase.Name, phase.Milliseconds);
		}

		for (const auto& pass : stats.RecordPasses)
		{
			profiler.Add("record", pass.Name, pass.Milliseconds);
		}

		double gpu = 0.0;

		for (const auto& pass : stats.GpuPasses)
		{
			profiler.Add("gpu", pass.Name, pass.Milliseconds);
			gpu += pass.Milliseconds;
		}

		if (stats.GpuTimingsAvailable)
		{
			profiler.Add("frame", "GPU (first begin to last end)", gpu);
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

		// Before scenes exit: their variables are still registered and get saved.
		if (commandRegistry && commandRegistry->IsConfigDirty())
		{
			if (const auto path = GetUserConfigPath(); !path.empty())
			{
				if (commandRegistry->SaveConfig(path))
				{
					commandRegistry->MarkConfigSaved();
				}
				else
				{
					std::cerr << "[Config] Could not write " << path.string() << '\n';
				}
			}
		}

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
		renderToggles.reset();
		consoleOverlay.reset();
		console.reset();
		uiRuntime.reset();
		renderServices = {};
		if (framePacing)
		{
			framePacing->SetDevice(nullptr);
		}

		if (renderQuality)
		{
			renderQuality->SetSettings(nullptr);
		}

		shaderHotReload.reset();
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
		windowSettings.reset();
		framePacing.reset();
		renderQuality.reset();
		shaderTweaks.reset();
		audioSystem.reset();
		commandRegistry.reset();
		actionMap.reset();
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
