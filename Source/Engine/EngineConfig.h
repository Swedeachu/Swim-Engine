#pragma once

#include "Engine/EngineState.h"
#include "Engine/Platform/Window.h"

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
		PresentMode Present{ PresentMode::Window };
		// One of Playing, Paused or Stopped.
		EngineState InitialState{ EngineState::Playing };
		// Overrides the application's startup scene when non-empty.
		std::string StartupScene;

		// False: no GPU at all (simulation, UI logic and tests only; --no-render).
		bool Render{ true };
		bool VSync{ true };
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
	//   --parent-hwnd=<handle>               (Windows embedding)
	// Every value form also accepts "--name value". Unknown arguments are errors.
	EngineConfigParseResult ParseEngineConfigArgs(int argc, char** argv);

	// One line per option, for --help.
	std::string GetEngineConfigUsage();
} // namespace Engine
