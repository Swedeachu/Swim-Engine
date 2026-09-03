#include "PCH.h"
#include "SwimEngine.h"
#include "Engine/Platform/MonotonicClock.h"
#include "Engine/Systems/Renderer/Vulkan/VulkanRenderer.h"
#include "Engine/Systems/Renderer/OpenGL/OpenGLRenderer.h"
#include "Engine/Systems/Renderer/OpenGL/ShaderToyRendererGL.h"
#include <cstdlib>
#include <filesystem>

namespace Engine
{

	std::shared_ptr<SwimEngine> EngineInstance = nullptr;

	std::string getDefaultWindowTitle()
	{
		std::string suffix;

	#if defined(_SWIM_DEBUG)
		suffix = " (Debug)";
	#else
		suffix = " (Release)";
	#endif

		if constexpr (SwimEngine::CONTEXT == SwimEngine::RenderContext::Vulkan)
		{
			return "Swim Engine [Vulkan]" + suffix;
		}
		else if constexpr (SwimEngine::CONTEXT == SwimEngine::RenderContext::OpenGL)
		{
			if constexpr (SwimEngine::useShaderToyIfOpenGL)
			{
				return "Swim Engine [OpenGL ShaderToy]" + suffix;
			}
			return "Swim Engine [OpenGL]" + suffix;
		}

		return "Swim Engine Demo" + suffix;
	}

	SwimEngine::SwimEngine(EngineArgs args)
	{
		Create(args.externalParentWindow, args.state);
	}

	SwimEngine::SwimEngine(std::uintptr_t externalParentWindow, EngineState state)
	{
		Create(externalParentWindow, state);
	}

	void SwimEngine::Create(std::uintptr_t externalParentWindow, EngineState state)
	{
		windowTitle = getDefaultWindowTitle();
		systemManager = std::make_unique<SystemManager>();
		platformSystem = std::make_unique<Swim::Platform::PlatformSystem>();
		this->externalParentWindow = externalParentWindow;
		this->engineState = state;
		ownsWindow = externalParentWindow == 0;
	}

	std::shared_ptr<SwimEngine> SwimEngine::GetInstance()
	{
		return EngineInstance;
	}

	std::shared_ptr<SwimEngine>& SwimEngine::GetInstanceRef()
	{
		return EngineInstance;
	}

	SwimEngine::EngineArgs SwimEngine::ParseStartingEngineArgs(int argc, char** argv)
	{
		std::uintptr_t externalParentWindow = 0;
		EngineState state = DefaultEngineState;

		for (int i = 1; i < argc; ++i)
		{
			std::string arg = argv[i];

			if (arg == "--parent-hwnd" && i + 1 < argc)
			{
				externalParentWindow = static_cast<std::uintptr_t>(std::strtoull(argv[++i], nullptr, 10));
			}
			else if (arg == "--state" && i + 1 < argc)
			{
				std::string value = argv[++i];
				EngineState parsed = ParseEngineStateArg(value);
				if (parsed != EngineState::None)
				{
					state = parsed;
				}
			}
			else if (arg.rfind("--state=", 0) == 0)
			{
				std::string value = arg.substr(std::string("--state=").size());
				EngineState parsed = ParseEngineStateArg(value);
				if (parsed != EngineState::None)
				{
					state = parsed;
				}
			}
		}

		return EngineArgs(externalParentWindow, state);
	}

	std::string SwimEngine::GetExecutableDirectory()
	{
		if (EngineInstance && EngineInstance->platformSystem && EngineInstance->platformSystem->IsInitialized())
		{
			return EngineInstance->platformSystem->GetFileSystem().GetExecutableDirectory().string();
		}

		return std::filesystem::current_path().string();
	}

	bool SwimEngine::Start()
	{
		if (auto self = shared_from_this(); self)
		{
			EngineInstance = self;
		}
		else
		{
			throw std::runtime_error("SwimEngine must be managed by a shared_ptr.");
		}

		if (Awake() == 0) return Init();

		return false;
	}

	int SwimEngine::Awake()
	{
		if (!MakeWindow()) return -1;

		return 0;
	}

	bool SwimEngine::MakeWindow()
	{
		Swim::Platform::PlatformDesc platformDesc{};
		platformDesc.OrganizationName = "Swim Services";
		platformDesc.ApplicationName = "Swim Engine";
		if (!platformSystem->Initialize(platformDesc))
		{
			return false;
		}

		Swim::Platform::WindowDesc windowDesc{};
		windowDesc.Title = windowTitle;
		windowDesc.Width = windowWidth;
		windowDesc.Height = windowHeight;
		windowDesc.Resizable = true;
		windowDesc.HighPixelDensity = true;

		if constexpr (CONTEXT == RenderContext::Vulkan)
		{
			windowDesc.GraphicsSupport = Swim::Platform::WindowGraphicsSupport::Vulkan;
		}
		else if constexpr (CONTEXT == RenderContext::OpenGL)
		{
			windowDesc.GraphicsSupport = Swim::Platform::WindowGraphicsSupport::OpenGL;
		}

		if (externalParentWindow != 0)
		{
			windowDesc.ExternalParent = {
				Swim::Platform::NativeWindowType::Win32,
				reinterpret_cast<void*>(externalParentWindow),
				nullptr
			};
		}

		engineWindow = platformSystem->GetWindowSystem().Create(windowDesc);
		if (!engineWindow)
		{
			return false;
		}

		engineWindow->Show();
		UpdateWindowSize();

		if (externalParentWindow != 0)
		{
			editorIpcBridge = std::make_unique<Swim::Platform::EditorIpcBridge>();
			Swim::Platform::NativeWindowHandle editorWindow{
				Swim::Platform::NativeWindowType::Win32,
				reinterpret_cast<void*>(externalParentWindow),
				nullptr
			};
			editorIpcBridge->Initialize(*engineWindow, editorWindow, [this](std::string_view message)
			{
				OnEditorCommand(message);
			});
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
		if constexpr (CONTEXT == RenderContext::OpenGL)
		{
			return *openglRenderer;
		}
		else
		{
			return *vulkanRenderer;
		}
	}

	int SwimEngine::Init()
	{
		inputManager = systemManager->AddSystem<InputManager>("InputManager");
		commandSystem = systemManager->AddSystem<CommandSystem>("CommandSystem");
		sceneSystem = systemManager->AddSystem<SceneSystem>("SceneSystem");
		physicsSystem = systemManager->AddSystem<PhysicsSystem>("PhysicsSystem");

		if constexpr (CONTEXT == RenderContext::Vulkan)
		{
			vulkanRenderer = systemManager->AddSystem<VulkanRenderer>("Renderer");
			vulkanRenderer->Create(*engineWindow, windowWidth, windowHeight);
		}
		else if constexpr (CONTEXT == RenderContext::OpenGL)
		{
			if constexpr (useShaderToyIfOpenGL)
			{
				openglRenderer = systemManager->AddSystem<ShaderToyRendererGL>("Renderer");
				openglRenderer->Create(*engineWindow, windowWidth, windowHeight);
			}
			else
			{
				openglRenderer = systemManager->AddSystem<OpenGLRenderer>("Renderer");
				openglRenderer->Create(*engineWindow, windowWidth, windowHeight);
			}
		}

		cameraSystem = systemManager->AddSystem<CameraSystem>("CameraSystem");

		if (systemManager->Awake() != 0)
		{
			return -1;
		}

		if (systemManager->Init() != 0)
		{
			return -1;
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

	void SwimEngine::RegisterVanillaEngineCommands()
	{
		SwimEngine* self = this;

		auto summarize = [](SwimEngine* e)
		{
			std::string s = "[Engine] State ->";
			if (HasAnyEngineStates(e->engineState, EngineState::Playing)) s += " Playing";
			if (HasAnyEngineStates(e->engineState, EngineState::Paused))  s += " Paused";
			if (HasAnyEngineStates(e->engineState, EngineState::Editing)) s += " Editing";
			if (HasAnyEngineStates(e->engineState, EngineState::Stopped)) s += " Stopped";
			if (!HasAnyEngineStates(e->engineState, EngineState::All))    s += " None";
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
		static double timeAccumulator = 0.0;
		static int frameCounter = 0;

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
			if constexpr (CONTEXT == RenderContext::Vulkan)
			{
				if (vulkanRenderer) { vulkanRenderer->SetFramebufferResized(); }
			}
			else if constexpr (CONTEXT == RenderContext::OpenGL)
			{
				if (openglRenderer) { openglRenderer->SetFramebufferResized(); }
			}

			needResize = false;
		}

		systemManager->Update(dt);

		timeAccumulator += dt;
		frameCounter++;

		if (timeAccumulator >= 1.0)
		{
			fps = static_cast<int>(static_cast<double>(frameCounter) / timeAccumulator);

			if (ownsWindow && engineWindow)
			{
				engineWindow->SetTitle(getDefaultWindowTitle() + " | " + std::to_string(fps) + " FPS");
			}

			timeAccumulator = 0.0;
			frameCounter = 0;
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

		systemManager->FixedUpdate(tickThisSecond);
	}

	int SwimEngine::Exit()
	{
		const int result = systemManager ? systemManager->Exit() : 0;
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

		if constexpr (CONTEXT == RenderContext::Vulkan)
		{
			if (vulkanRenderer)
			{
				vulkanRenderer->SetSurfaceSize(windowWidth, windowHeight);
			}
		}
		else if constexpr (CONTEXT == RenderContext::OpenGL)
		{
			if (openglRenderer)
			{
				openglRenderer->SetSurfaceSize(windowWidth, windowHeight);
			}
		}
	}

}
