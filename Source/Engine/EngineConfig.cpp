#include "Engine/EngineConfig.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <sstream>

namespace Engine
{
	namespace
	{
		std::string Lower(std::string_view value)
		{
			std::string out(value);
			std::transform(out.begin(), out.end(), out.begin(),
				[](unsigned char ch)
				{
					return static_cast<char>(std::tolower(ch));
				});
			return out;
		}

		bool ParseGraphicsBackend(std::string_view value, GraphicsBackend& backend)
		{
			const std::string v = Lower(value);
			if (v == "auto")
			{
				backend = GraphicsBackend::Auto;
			}
			else if (v == "vulkan")
			{
				backend = GraphicsBackend::Vulkan;
			}
			else if (v == "d3d12")
			{
				backend = GraphicsBackend::D3D12;
			}
			else if (v == "metal")
			{
				backend = GraphicsBackend::Metal;
			}
			else
			{
				return false;
			}
			return true;
		}

		bool ParsePhysicsBackend(std::string_view value, PhysicsBackend& backend)
		{
			const std::string v = Lower(value);
			if (v == "auto")
			{
				backend = PhysicsBackend::Auto;
			}
			else if (v == "physx")
			{
				backend = PhysicsBackend::PhysX;
			}
			else if (v == "jolt")
			{
				backend = PhysicsBackend::Jolt;
			}
			else
			{
				return false;
			}
			return true;
		}

		bool ParseUnsigned(std::string_view value, std::uint64_t& result)
		{
			result = 0;
			const char* begin = value.data();
			const char* end = begin + value.size();
			auto [ptr, error] = std::from_chars(begin, end, result);
			return !value.empty() && error == std::errc{} && ptr == end;
		}

		bool ParseDouble(std::string_view value, double& result)
		{
			if (value.empty())
			{
				return false;
			}
			const std::string text(value);
			char* end = nullptr;
			result = std::strtod(text.c_str(), &end);
			return end == text.c_str() + text.size();
		}

		bool ParseBool(std::string_view value, bool& result)
		{
			const std::string v = Lower(value);
			if (v == "on" || v == "true" || v == "1" || v == "yes")
			{
				result = true;
				return true;
			}
			if (v == "off" || v == "false" || v == "0" || v == "no")
			{
				result = false;
				return true;
			}
			return false;
		}

		bool ParseInitialState(std::string_view value, EngineState& state)
		{
			const EngineState parsed = ParseEngineStateToken(std::string(value));
			if (!IsSingleEngineState(parsed))
			{
				return false;
			}
			state = parsed;
			return true;
		}

		// Matches "--name", "--name=value" or "--name value". Returns false when the
		// argument is a different option.
		bool Option(
			std::string_view argument, std::string_view name, int& index, int argc, char** argv, std::string_view& value, bool& hasValue)
		{
			if (argument.rfind(name, 0) != 0)
			{
				return false;
			}
			const std::string_view rest = argument.substr(name.size());
			if (rest.empty())
			{
				if (index + 1 < argc && std::string_view(argv[index + 1]).rfind("--", 0) != 0)
				{
					value = argv[++index];
					hasValue = true;
				}
				else
				{
					value = {};
					hasValue = false;
				}
				return true;
			}
			if (rest.front() != '=')
			{
				return false;
			}
			value = rest.substr(1);
			hasValue = true;
			return true;
		}
	} // namespace

	GraphicsBackend ResolveGraphicsBackend(GraphicsBackend backend)
	{
		return backend == GraphicsBackend::Auto ? GraphicsBackend::Vulkan : backend;
	}

	PhysicsBackend ResolvePhysicsBackend(PhysicsBackend backend, bool physXAvailable, bool joltAvailable)
	{
		switch (backend)
		{
		case PhysicsBackend::Auto:
			return physXAvailable ? PhysicsBackend::PhysX : (joltAvailable ? PhysicsBackend::Jolt : PhysicsBackend::Auto);
		case PhysicsBackend::PhysX:
			return physXAvailable ? PhysicsBackend::PhysX : PhysicsBackend::Auto;
		case PhysicsBackend::Jolt:
			return joltAvailable ? PhysicsBackend::Jolt : PhysicsBackend::Auto;
		}
		return PhysicsBackend::Auto;
	}

	std::string_view ToString(GraphicsBackend backend)
	{
		switch (backend)
		{
		case GraphicsBackend::Auto:
			return "Auto";
		case GraphicsBackend::Vulkan:
			return "Vulkan";
		case GraphicsBackend::D3D12:
			return "D3D12";
		case GraphicsBackend::Metal:
			return "Metal";
		}
		return "Unknown";
	}

	std::string_view ToString(PhysicsBackend backend)
	{
		switch (backend)
		{
		case PhysicsBackend::Auto:
			return "Auto";
		case PhysicsBackend::PhysX:
			return "PhysX";
		case PhysicsBackend::Jolt:
			return "Jolt";
		}
		return "Unknown";
	}

	std::string_view ToString(PresentMode mode)
	{
		switch (mode)
		{
		case PresentMode::Window:
			return "Window";
		case PresentMode::Headless:
			return "Headless";
		}
		return "Unknown";
	}

	EngineConfigParseResult ParseEngineConfigArgs(int argc, char** argv)
	{
		EngineConfigParseResult result{};
		EngineConfig& config = result.Config;

		for (int i = 1; i < argc; ++i)
		{
			const std::string_view argument = argv[i];
			std::string_view value;
			bool hasValue = false;
			const auto fail = [&](std::string message)
			{
				result.Errors.push_back(std::move(message));
			};

			if (argument == "--help" || argument == "-h")
			{
				config.ShowHelp = true;
			}
			else if (Option(argument, "--graphics", i, argc, argv, value, hasValue))
			{
				if (!hasValue || !ParseGraphicsBackend(value, config.Graphics))
				{
					fail("Invalid --graphics value. Expected auto, vulkan, d3d12 or metal.");
				}
			}
			else if (Option(argument, "--physics", i, argc, argv, value, hasValue))
			{
				if (!hasValue || !ParsePhysicsBackend(value, config.Physics))
				{
					fail("Invalid --physics value. Expected auto, physx or jolt.");
				}
			}
			else if (Option(argument, "--state", i, argc, argv, value, hasValue))
			{
				if (!hasValue || !ParseInitialState(value, config.InitialState))
				{
					fail("Invalid --state value. Expected playing, paused or stopped.");
				}
			}
			else if (Option(argument, "--scene", i, argc, argv, value, hasValue))
			{
				if (!hasValue || value.empty())
				{
					fail("--scene requires a scene name.");
				}
				else
				{
					config.StartupScene = std::string(value);
				}
			}
			else if (Option(argument, "--width", i, argc, argv, value, hasValue) ||
				Option(argument, "--height", i, argc, argv, value, hasValue))
			{
				std::uint64_t pixels = 0;
				const bool width = argument.rfind("--width", 0) == 0;
				if (!hasValue || !ParseUnsigned(value, pixels) || pixels < 16 || pixels > 16384)
				{
					fail(width ? "Invalid --width value (16..16384)." : "Invalid --height value (16..16384).");
				}
				else if (width)
				{
					config.Window.Width = static_cast<std::uint32_t>(pixels);
				}
				else
				{
					config.Window.Height = static_cast<std::uint32_t>(pixels);
				}
			}
			else if (Option(argument, "--size", i, argc, argv, value, hasValue))
			{
				const std::size_t x = hasValue ? value.find_first_of("xX") : std::string_view::npos;
				std::uint64_t w = 0;
				std::uint64_t h = 0;
				if (x == std::string_view::npos || !ParseUnsigned(value.substr(0, x), w) || !ParseUnsigned(value.substr(x + 1), h) ||
					w < 16 || h < 16 || w > 16384 || h > 16384)
				{
					fail("Invalid --size value. Expected <width>x<height>.");
				}
				else
				{
					config.Window.Width = static_cast<std::uint32_t>(w);
					config.Window.Height = static_cast<std::uint32_t>(h);
				}
			}
			else if (argument == "--no-vsync")
			{
				config.VSync = false;
			}
			else if (Option(argument, "--vsync", i, argc, argv, value, hasValue))
			{
				if (hasValue && !ParseBool(value, config.VSync))
				{
					fail("Invalid --vsync value. Expected on or off.");
				}
				else if (!hasValue)
				{
					config.VSync = true;
				}
			}
			else if (Option(argument, "--validation", i, argc, argv, value, hasValue))
			{
				if (hasValue && !ParseBool(value, config.Validation))
				{
					fail("Invalid --validation value. Expected on or off.");
				}
				else if (!hasValue)
				{
					config.Validation = true;
				}
			}
			else if (Option(argument, "--fixed-rate", i, argc, argv, value, hasValue))
			{
				if (!hasValue || !ParseDouble(value, config.FixedRate) || config.FixedRate < 1.0 || config.FixedRate > 1000.0)
				{
					fail("Invalid --fixed-rate value (1..1000 Hz).");
				}
			}
			else if (Option(argument, "--time-scale", i, argc, argv, value, hasValue))
			{
				if (!hasValue || !ParseDouble(value, config.TimeScale) || config.TimeScale < 0.0 || config.TimeScale > 100.0)
				{
					fail("Invalid --time-scale value (0..100).");
				}
			}
			else if (argument == "--headless")
			{
				config.Present = PresentMode::Headless;
			}
			else if (argument == "--no-render")
			{
				config.Render = false;
				config.Present = PresentMode::Headless;
			}
			else if (Option(argument, "--frames", i, argc, argv, value, hasValue))
			{
				if (!hasValue || !ParseUnsigned(value, config.MaxFrames) || config.MaxFrames == 0)
				{
					fail("Invalid --frames value (a positive frame count).");
				}
			}
			else if (Option(argument, "--capture", i, argc, argv, value, hasValue))
			{
				if (!hasValue || value.empty())
				{
					fail("--capture requires an output path.");
				}
				else
				{
					config.CapturePath = std::string(value);
				}
			}
			else if (Option(argument, "--fixed-delta", i, argc, argv, value, hasValue))
			{
				if (!hasValue || !ParseDouble(value, config.FixedFrameDelta) || config.FixedFrameDelta <= 0.0 ||
					config.FixedFrameDelta > 1.0)
				{
					fail("Invalid --fixed-delta value (0..1 seconds).");
				}
			}
			else if (Option(argument, "--exec", i, argc, argv, value, hasValue))
			{
				if (!hasValue || value.empty())
				{
					fail("--exec requires a command.");
				}
				else
				{
					config.StartupCommands.emplace_back(value);
				}
			}
			else if (Option(argument, "--parent-hwnd", i, argc, argv, value, hasValue))
			{
				std::uint64_t nativeValue = 0;
				if (!hasValue || !ParseUnsigned(value, nativeValue) || nativeValue == 0)
				{
					fail("Invalid --parent-hwnd value.");
				}
				else
				{
					config.Window.ExternalParent = { Swim::Platform::NativeWindowType::Win32,
						reinterpret_cast<void*>(static_cast<std::uintptr_t>(nativeValue)), nullptr };
				}
			}
			else
			{
				fail("Unknown argument '" + std::string(argument) + "' (see --help).");
			}
		}

		if (!config.Render && !config.CapturePath.empty())
		{
			result.Errors.emplace_back("--capture needs rendering (drop --no-render).");
		}
		if (!config.CapturePath.empty() && config.MaxFrames == 0)
		{
			// A capture without a frame budget captures after the first few frames.
			config.MaxFrames = 60;
		}
		return result;
	}

	std::string GetEngineConfigUsage()
	{
		std::ostringstream out;
		out << "Swim Engine options:\n"
			<< "  --graphics=auto|vulkan|d3d12|metal  Graphics backend (auto = vulkan)\n"
			<< "  --physics=auto|physx|jolt           Physics backend (auto = physx if built, else jolt)\n"
			<< "  --state=playing|paused|stopped      Initial engine state\n"
			<< "  --scene=<name>                      Startup scene\n"
			<< "  --width=<px> --height=<px>          Window size (or --size=<W>x<H>)\n"
			<< "  --vsync=on|off, --no-vsync          Present with/without vsync\n"
			<< "  --validation[=on|off]               GPU validation layers\n"
			<< "  --fixed-rate=<hz>                   Fixed simulation rate (default 60)\n"
			<< "  --time-scale=<scale>                Initial simulation time scale\n"
			<< "  --headless                          Render offscreen without a window\n"
			<< "  --no-render                         Run without a GPU (simulation and UI logic only)\n"
			<< "  --frames=<n>                        Exit after n frames\n"
			<< "  --capture=<file.ppm>                Save the last frame (implies --frames=60)\n"
			<< "  --fixed-delta=<seconds>             Deterministic frame delta\n"
			<< "  --exec=<command>                    Run an engine command after startup (repeatable)\n"
			<< "  --parent-hwnd=<handle>              Embed in a native parent window (Windows)\n";
		return out.str();
	}
} // namespace Engine
