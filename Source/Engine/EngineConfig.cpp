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

		// Options without a value.
		struct FlagOption
		{
			std::string_view Name;
			void (*Apply)(EngineConfig& config);
		};

		const FlagOption FlagOptions[] = {
			{ "--help",
				[](EngineConfig& config)
				{
					config.ShowHelp = true;
				} },
			{ "-h",
				[](EngineConfig& config)
				{
					config.ShowHelp = true;
				} },
			{ "--no-activate",
				[](EngineConfig& config)
				{
					config.Window.Activate = false;
				} },
			{ "--no-vsync",
				[](EngineConfig& config)
				{
					config.VSync = false;
				} },
			{ "--headless",
				[](EngineConfig& config)
				{
					config.Present = PresentMode::Headless;
				} },
			{ "--no-render",
				[](EngineConfig& config)
				{
					config.Render = false;
					config.Present = PresentMode::Headless;
				} },
		};

		// Options that take a value (--name=value or --name value); errors collects bad values.
		struct ValueOption
		{
			std::string_view Name;
			void (*Apply)(EngineConfig& config, std::string_view value, bool hasValue, std::vector<std::string>& errors);
		};

		const ValueOption ValueOptions[] = {
			{ "--graphics",
				[](EngineConfig& config, std::string_view value, bool hasValue, std::vector<std::string>& errors)
				{
					if (!hasValue || !ParseGraphicsBackend(value, config.Graphics))
					{
						errors.emplace_back("Invalid --graphics value. Expected auto, vulkan, d3d12 or metal.");
					}
				} },
			{ "--rhi",
				[](EngineConfig& config, std::string_view value, bool hasValue, std::vector<std::string>& errors)
				{
					if (!hasValue || !ParseGraphicsBackend(value, config.Graphics))
					{
						errors.emplace_back("Invalid --rhi value. Expected auto, vulkan, d3d12 or metal.");
					}
				} },
			{ "--physics",
				[](EngineConfig& config, std::string_view value, bool hasValue, std::vector<std::string>& errors)
				{
					if (!hasValue || !ParsePhysicsBackend(value, config.Physics))
					{
						errors.emplace_back("Invalid --physics value. Expected auto, physx or jolt.");
					}
				} },
			{ "--state",
				[](EngineConfig& config, std::string_view value, bool hasValue, std::vector<std::string>& errors)
				{
					if (!hasValue || !ParseInitialState(value, config.InitialState))
					{
						errors.emplace_back("Invalid --state value. Expected playing, paused or stopped.");
					}
				} },
			{ "--assets",
				[](EngineConfig& config, std::string_view value, bool hasValue, std::vector<std::string>& errors)
				{
					if (!hasValue || value.empty())
					{
						errors.emplace_back("--assets requires a directory.");
					}
					else
					{
						config.AssetRoot = std::string(value);
					}
				} },
			{ "--scene",
				[](EngineConfig& config, std::string_view value, bool hasValue, std::vector<std::string>& errors)
				{
					if (!hasValue || value.empty())
					{
						errors.emplace_back("--scene requires a scene name.");
					}
					else
					{
						config.StartupScene = std::string(value);
					}
				} },
			{ "--width",
				[](EngineConfig& config, std::string_view value, bool hasValue, std::vector<std::string>& errors)
				{
					std::uint64_t pixels = 0;

					if (!hasValue || !ParseUnsigned(value, pixels) || pixels < 16 || pixels > 16384)
					{
						errors.emplace_back("Invalid --width value (16..16384).");
					}
					else
					{
						config.Window.Width = static_cast<std::uint32_t>(pixels);
					}
				} },
			{ "--height",
				[](EngineConfig& config, std::string_view value, bool hasValue, std::vector<std::string>& errors)
				{
					std::uint64_t pixels = 0;

					if (!hasValue || !ParseUnsigned(value, pixels) || pixels < 16 || pixels > 16384)
					{
						errors.emplace_back("Invalid --height value (16..16384).");
					}
					else
					{
						config.Window.Height = static_cast<std::uint32_t>(pixels);
					}
				} },
			{ "--input",
				[](EngineConfig& config, std::string_view value, bool hasValue, std::vector<std::string>& errors)
				{
					if (hasValue && (value == "none" || value == "all"))
					{
						config.PlatformInput = value == "all";
						config.Window.CaptureMouse = config.PlatformInput;
					}
					else
					{
						errors.emplace_back("Invalid --input value. Expected none or all.");
					}
				} },
			{ "--display",
				[](EngineConfig& config, std::string_view value, bool hasValue, std::vector<std::string>& errors)
				{
					std::uint64_t index = 0;

					config.DisplayGiven = true;

					if (hasValue && value == "primary")
					{
						config.Window.DisplayIndex = 0;
						config.Window.Display = 0;
					}
					else if (!hasValue || !ParseUnsigned(value, index) || index < 1 || index > 64)
					{
						errors.emplace_back("Invalid --display value. Expected 'primary' or a monitor number from 1.");
					}
					else
					{
						config.Window.DisplayIndex = static_cast<std::uint32_t>(index);
					}
				} },
			{ "--size",
				[](EngineConfig& config, std::string_view value, bool hasValue, std::vector<std::string>& errors)
				{
					const std::size_t x = hasValue ? value.find_first_of("xX") : std::string_view::npos;
					std::uint64_t w = 0;
					std::uint64_t h = 0;

					if (x == std::string_view::npos || !ParseUnsigned(value.substr(0, x), w) || !ParseUnsigned(value.substr(x + 1), h) ||
						w < 16 || h < 16 || w > 16384 || h > 16384)
					{
						errors.emplace_back("Invalid --size value. Expected <width>x<height>.");
					}
					else
					{
						config.Window.Width = static_cast<std::uint32_t>(w);
						config.Window.Height = static_cast<std::uint32_t>(h);
					}
				} },
			{ "--audio",
				[](EngineConfig& config, std::string_view value, bool, std::vector<std::string>& errors)
				{
					if (value == "device" || value == "on")
					{
						config.Audio = AudioOutput::Device;
					}
					else if (value == "null")
					{
						config.Audio = AudioOutput::Null;
					}
					else if (value == "off")
					{
						config.Audio = AudioOutput::Off;
					}
					else
					{
						errors.emplace_back("Invalid --audio value. Expected device, null or off.");
					}
				} },
			{ "--vsync",
				[](EngineConfig& config, std::string_view value, bool hasValue, std::vector<std::string>& errors)
				{
					if (hasValue && !ParseBool(value, config.VSync))
					{
						errors.emplace_back("Invalid --vsync value. Expected on or off.");
					}
					else if (!hasValue)
					{
						config.VSync = true;
					}
				} },
			{ "--validation",
				[](EngineConfig& config, std::string_view value, bool hasValue, std::vector<std::string>& errors)
				{
					if (hasValue && !ParseBool(value, config.Validation))
					{
						errors.emplace_back("Invalid --validation value. Expected on or off.");
					}
					else if (!hasValue)
					{
						config.Validation = true;
					}
				} },
			{ "--fixed-rate",
				[](EngineConfig& config, std::string_view value, bool hasValue, std::vector<std::string>& errors)
				{
					if (!hasValue || !ParseDouble(value, config.FixedRate) || config.FixedRate < 1.0 || config.FixedRate > 1000.0)
					{
						errors.emplace_back("Invalid --fixed-rate value (1..1000 Hz).");
					}
				} },
			{ "--time-scale",
				[](EngineConfig& config, std::string_view value, bool hasValue, std::vector<std::string>& errors)
				{
					if (!hasValue || !ParseDouble(value, config.TimeScale) || config.TimeScale < 0.0 || config.TimeScale > 100.0)
					{
						errors.emplace_back("Invalid --time-scale value (0..100).");
					}
				} },
			{ "--adapter",
				[](EngineConfig& config, std::string_view value, bool hasValue, std::vector<std::string>& errors)
				{
					if (!hasValue || value.empty())
					{
						errors.emplace_back("Invalid --adapter value (auto, display, discrete, integrated, an index or a name).");
					}
					else
					{
						config.Adapter = std::string(value);
					}
				} },
			{ "--present-split",
				[](EngineConfig& config, std::string_view value, bool hasValue, std::vector<std::string>& errors)
				{
					if (value == "auto")
					{
						config.SplitPresent = 0;
					}
					else if (bool on = false; hasValue && ParseBool(value, on))
					{
						config.SplitPresent = on ? 1 : 2;
					}
					else
					{
						errors.emplace_back("Invalid --present-split value (auto, on or off).");
					}
				} },
			{ "--present-defer",
				[](EngineConfig& config, std::string_view value, bool hasValue, std::vector<std::string>& errors)
				{
					if (hasValue && !ParseBool(value, config.DeferPresent))
					{
						errors.emplace_back("Invalid --present-defer value (on or off).");
					}
				} },
			{ "--present-thread",
				[](EngineConfig& config, std::string_view value, bool hasValue, std::vector<std::string>& errors)
				{
					if (hasValue && !ParseBool(value, config.PresentThread))
					{
						errors.emplace_back("Invalid --present-thread value (on or off).");
					}
				} },
			{ "--frames-in-flight",
				[](EngineConfig& config, std::string_view value, bool hasValue, std::vector<std::string>& errors)
				{
					std::uint64_t frames = 0;

					if (!hasValue || !ParseUnsigned(value, frames) || frames < 1 || frames > 2)
					{
						errors.emplace_back("Invalid --frames-in-flight value (1 or 2).");
					}
					else
					{
						config.FramesInFlight = static_cast<std::uint32_t>(frames);
					}
				} },
			{ "--frames",
				[](EngineConfig& config, std::string_view value, bool hasValue, std::vector<std::string>& errors)
				{
					if (!hasValue || !ParseUnsigned(value, config.MaxFrames) || config.MaxFrames == 0)
					{
						errors.emplace_back("Invalid --frames value (a positive frame count).");
					}
				} },
			{ "--capture",
				[](EngineConfig& config, std::string_view value, bool hasValue, std::vector<std::string>& errors)
				{
					if (!hasValue || value.empty())
					{
						errors.emplace_back("--capture requires an output path.");
					}
					else
					{
						config.CapturePath = std::string(value);
					}
				} },
			{ "--fixed-delta",
				[](EngineConfig& config, std::string_view value, bool hasValue, std::vector<std::string>& errors)
				{
					if (!hasValue || !ParseDouble(value, config.FixedFrameDelta) || config.FixedFrameDelta <= 0.0 ||
						config.FixedFrameDelta > 1.0)
					{
						errors.emplace_back("Invalid --fixed-delta value (0..1 seconds).");
					}
				} },
			{ "--exec",
				[](EngineConfig& config, std::string_view value, bool hasValue, std::vector<std::string>& errors)
				{
					if (!hasValue || value.empty())
					{
						errors.emplace_back("--exec requires a command.");
					}
					else
					{
						config.StartupCommands.emplace_back(value);
					}
				} },
			{ "--window-mode",
				[](EngineConfig& config, std::string_view value, bool, std::vector<std::string>& errors)
				{
					config.WindowModeGiven = true;

					if (value == "windowed")
					{
						config.Window.Mode = Swim::Platform::WindowMode::Windowed;
					}
					else if (value == "borderless")
					{
						config.Window.Mode = Swim::Platform::WindowMode::Borderless;
					}
					else if (value == "exclusive")
					{
						config.Window.Mode = Swim::Platform::WindowMode::ExclusiveFullscreen;
					}
					else
					{
						errors.emplace_back("Invalid --window-mode value. Expected windowed, borderless or exclusive.");
					}
				} },
			{ "--config",
				[](EngineConfig& config, std::string_view value, bool hasValue, std::vector<std::string>& errors)
				{
					if (!hasValue || value.empty())
					{
						errors.emplace_back("--config requires a file name or none.");
					}
					else
					{
						config.UserConfigFile = value == "none" ? std::string() : std::string(value);
					}
				} },
			{ "--parent-hwnd",
				[](EngineConfig& config, std::string_view value, bool hasValue, std::vector<std::string>& errors)
				{
					std::uint64_t nativeValue = 0;

					if (!hasValue || !ParseUnsigned(value, nativeValue) || nativeValue == 0)
					{
						errors.emplace_back("Invalid --parent-hwnd value.");
					}
					else
					{
						config.Window.ExternalParent = { Swim::Platform::NativeWindowType::Win32,
							reinterpret_cast<void*>(static_cast<std::uintptr_t>(nativeValue)), nullptr };
					}
				} },
		};

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
			const auto flag = std::find_if(std::begin(FlagOptions), std::end(FlagOptions),
				[&](const FlagOption& option)
				{
					return option.Name == argument;
				});

			if (flag != std::end(FlagOptions))
			{
				flag->Apply(config);
				continue;
			}

			std::string_view value;
			bool hasValue = false;
			bool matched = false;

			for (const auto& option : ValueOptions)
			{
				if (Option(argument, option.Name, i, argc, argv, value, hasValue))
				{
					option.Apply(config, value, hasValue, result.Errors);
					matched = true;
					break;
				}
			}

			if (!matched)
			{
				result.Errors.push_back("Unknown argument '" + std::string(argument) + "' (see --help).");
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
			<< "  --graphics=auto|vulkan|d3d12|metal  Graphics backend (auto = vulkan; --rhi= is the same option)\n"
			<< "  --physics=auto|physx|jolt           Physics backend (auto = physx if built, else jolt)\n"
			<< "  --state=playing|paused|stopped      Initial engine state\n"
			<< "  --scene=<name>                      Startup scene\n"
			<< "  --assets=<dir>                      Loose-asset root (default: the repository's Assets/)\n"
			<< "  --width=<px> --height=<px>          Window size (or --size=<W>x<H>)\n"
			<< "  --display=<n|primary>               Monitor to open on, numbered from 1\n"
			<< "  --no-activate                       Open the window without taking focus\n"
			<< "  --input=<all|none>                  none: ignore OS keyboard and mouse, never capture the cursor\n"
			<< "  --vsync=on|off, --no-vsync          Present with/without vsync\n"
			<< "  --audio=device|null|off             Audio output (null: no device, voices still advance)\n"
			<< "  --validation[=on|off]               GPU validation layers\n"
			<< "  --fixed-rate=<hz>                   Fixed simulation rate (default 60)\n"
			<< "  --time-scale=<scale>                Initial simulation time scale\n"
			<< "  --headless                          Render offscreen without a window\n"
			<< "  --no-render                         Run without a GPU (simulation and UI logic only)\n"
			<< "  --frames=<n>                        Exit after n frames\n"
			<< "  --frames-in-flight=1|2              Frames recorded ahead of the GPU (default 2)\n"
			<< "  --adapter=auto|display|discrete|integrated|<index>|<name>  GPU to render with (auto: the display's if discrete)\n"
			<< "  --present-thread=on|off             Present on a dedicated thread (default on)\n"
			<< "  --present-split=auto|on|off         Submit frames before acquiring their image (auto: cross-adapter displays)\n"
			<< "  --capture=<file.ppm>                Save the last frame (implies --frames=60)\n"
			<< "  --fixed-delta=<seconds>             Deterministic frame delta\n"
			<< "  --exec=<command>                    Run an engine command after startup (repeatable)\n"
			<< "  --window-mode=windowed|borderless|exclusive  Initial window mode\n"
			<< "  --config=<file>|none                User config file under the user-data root (default config.cfg)\n"
			<< "  --parent-hwnd=<handle>              Embed in a native parent window (Windows)\n";
		return out.str();
	}

} // namespace Engine
