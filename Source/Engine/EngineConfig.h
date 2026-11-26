#pragma once

#include "Engine/EngineState.h"
#include "Engine/Platform/Window.h"
#include "Engine/Systems/Audio/AudioSystem.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Engine
{

	// The engine renders through the modern RHI only. Vulkan is the one implemented
	// backend today; D3D12/Metal are reserved names that fail at startup until their
	// RHI backends exist.
	enum class GraphicsBackend : uint8_t
	{
		Auto,
		Vulkan,
		D3D12,
		Metal
	};

	enum class PhysicsBackend : uint8_t
	{
		Auto,
		PhysX,
		Jolt
	};

	// Where frames go.
	enum class PresentMode : uint8_t
	{
		Window,	  // A swapchain on the engine window.
		Headless, // No window: frames render to an offscreen target (capture/CI).
	};

	struct EngineConfig
	{
		EngineConfig() { Window.Title = "Swim Engine"; }

		GraphicsBackend Graphics{ GraphicsBackend::Auto };
		PhysicsBackend Physics{ PhysicsBackend::Auto };
		Swim::Platform::WindowDesc Window{};
		// Given on the command line: these win over the saved window_mode / window_display for the run.
		bool WindowModeGiven = false;
		bool DisplayGiven = false;
		PresentMode Present{ PresentMode::Window };
		// One of Playing, Paused or Stopped.
		EngineState InitialState{ EngineState::Playing };
		// Name the per-user folders (settings, caches) are created under; set these to the
		// application's own names so it does not share the user-data root of other games.
		std::string OrganizationName{ "Swim Services" };
		std::string ApplicationName{ "Swim Engine" };
		// The user config file (saved console variables and input bindings), relative to the
		// user-data root unless absolute. Loaded after startup, before --exec commands, and
		// written on exit when a saved setting changed. Empty: no config file (--config=none).
		std::string UserConfigFile{ "config.cfg" };
		// Default of the saved look_sensitivity setting (0..100; 50 is one unit of look per
		// unit of raw mouse motion, the curve is cubic so both ends stay usable).
		double LookSensitivityDefault{ 50.0 };
		// Overrides the application's startup scene when non-empty.
		std::string StartupScene;
		// Loose-asset root (--assets). Empty: the repository's Assets/ folder in development
		// builds (SWIM_DEVELOPMENT_ASSET_ROOT) when it exists, else <exe dir>/Assets.
		std::string AssetRoot;

		// --audio=device|null|off. Headless runs use the null output unless audio is off.
		AudioOutput Audio{ AudioOutput::Device };

		// False: no GPU at all (simulation, UI logic and tests only; --no-render).
		bool Render{ true };
		// False (--input=none): keyboard and mouse from the OS are ignored and the cursor is
		// never captured; only input the application injects drives it (automated runs).
		bool PlatformInput{ true };
		// Off by default (mailbox, else immediate): the frame rate is not capped at the
		// display's refresh. --vsync=on presents FIFO.
		bool VSync{ false };
		// Frames the CPU records ahead of the GPU (--frames-in-flight=1|2).
		std::uint32_t FramesInFlight{ 2 };
		// The GPU (--adapter=auto|display|discrete|integrated|<index>|<part of the name>; see
		// RenderDeviceDesc::Adapter) and whether presents run on their own thread (--present-thread).
		std::string Adapter{ "auto" };
		bool PresentThread{ true };
		// Present each frame after the next is submitted (--present-defer): see RenderDeviceDesc.
		bool DeferPresent{ false };
		// --present-split=auto|on|off: see RenderDeviceDesc::SplitPresent (0 auto, 1 on, 2 off).
		std::uint8_t SplitPresent{ 0 };
		// GPU validation layers (debug builds enable them by default).
		bool Validation{ DefaultValidation };
		// Simulation fixed rate in Hz and initial time scale.
		double FixedRate{ 60.0 };
		double TimeScale{ 1.0 };

		// Automation: exit after this many frames (0 = run until closed) and write the
		// last rendered frame to this path (.ppm) before exiting.
		std::uint64_t MaxFrames{ 0 };
		std::string CapturePath;
		// Fixed wall-clock delta per frame for deterministic runs (0 = real time).
		double FixedFrameDelta{ 0.0 };
		// Console commands run once after startup, in order (--exec, repeatable), for
		// scripted runs: "camera 0 5 20 0 1 0", "pause", "timescale 0.5", ...
		std::vector<std::string> StartupCommands;
		// --help was passed: print GetEngineConfigUsage() and exit.
		bool ShowHelp{ false };

		static constexpr bool DefaultValidation =
#if defined(_SWIM_DEBUG)
			true;
#else
			false;
#endif
	};

	struct EngineConfigParseResult
	{
		EngineConfig Config{};
		std::vector<std::string> Errors;

		bool IsValid() const { return Errors.empty(); }

		explicit operator bool() const { return IsValid(); }
	};

	GraphicsBackend ResolveGraphicsBackend(GraphicsBackend backend);
	// Auto picks PhysX where it is compiled in, else Jolt. Returns Auto when the
	// requested backend is not available.
	PhysicsBackend ResolvePhysicsBackend(PhysicsBackend backend, bool physXAvailable, bool joltAvailable);

	std::string_view ToString(GraphicsBackend backend);
	std::string_view ToString(PhysicsBackend backend);
	std::string_view ToString(PresentMode mode);

	// Command line:
	//   --graphics=auto|vulkan|d3d12|metal   --physics=auto|physx|jolt
	//   --state=playing|paused|stopped       --scene=<name>
	//   --width=<px> --height=<px> | --size=<W>x<H>
	//   --vsync=on|off | --no-vsync          --validation[=on|off]
	//   --fixed-rate=<hz>                     --time-scale=<scale>
	//   --headless  --no-render               --frames=<n>
	//   --capture=<file.ppm>                  --fixed-delta=<seconds>
	//   --exec=<command>                      (repeatable; runs after startup)
	//   --config=<file>|none                  User config file (default config.cfg)
	//   --window-mode=windowed|borderless|exclusive
	//   --parent-hwnd=<handle>               (Windows embedding)
	// Every value form also accepts "--name value". Unknown arguments are errors.
	EngineConfigParseResult ParseEngineConfigArgs(int argc, char** argv);

	// One line per option, for --help.
	std::string GetEngineConfigUsage();

} // namespace Engine
