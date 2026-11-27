#include "Engine/SwimEngine.h"

#include "Engine/Runtime/RuntimeConsole.h"
#include "Engine/Runtime/RuntimeConsoleOverlay.h"
#include "Engine/Components/CameraComponent.h"
#include "Engine/Input/InputNames.h"
#include "Engine/Math/VectorMath.h"
#include "Engine/Components/Transform.h"
#include "Engine/Runtime/UiRuntime.h"
#include "Engine/Runtime/FramePacing.h"
#include "Engine/Systems/Audio/AudioSystem.h"
#include "Engine/Runtime/RenderQuality.h"
#include "Engine/Runtime/ShaderHotReload.h"
#include "Engine/Runtime/ShaderTweaks.h"
#include "Engine/Runtime/WindowSettings.h"
#include "Engine/Systems/Camera/CameraSystem.h"
#include "Engine/Systems/Renderer/Geometry/GeometryHeap.h"
#include "Engine/Systems/Renderer/Residency/AssetResidencyService.h"
#include "Engine/Systems/Renderer/Resources/BindlessResourceTable.h"
#include "Engine/Systems/Renderer/Runtime/FrameRenderer.h"
#include "Engine/Systems/Renderer/Runtime/MaterialLibrary.h"
#include "Engine/Systems/Renderer/Runtime/MeshLibrary.h"
#include "Engine/Systems/Renderer/Runtime/RenderDevice.h"
#include "Engine/Systems/Renderer/Runtime/ShaderLibrary.h"
#include "Engine/Systems/Scene/RenderExtraction/Runtime/SceneRenderBridge.h"
#include "Engine/Systems/Scene/SceneSystem.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cstdio>
#include <optional>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace Engine
{

	void SwimEngine::RegisterEngineCommands()
	{
		RegisterSessionCommands();
		RegisterCaptureCommands();
		RegisterProfilingCommands();
		RegisterBenchCommands();
		RegisterConsoleCommands();
	}

	void SwimEngine::RegisterSessionCommands()
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
		commands.Register({ "resource_stats", "Prints live GPU resources: bindless slots, textures, meshes, materials, pending releases",
							  "Video/Rendering", "" },
			[this](const std::vector<std::string>&)
			{
				if (!renderServices.Renderer)
				{
					std::cout << "No renderer.\n";
					return;
				}

				const auto& renderer = *renderServices.Renderer;
				const auto bindless = renderer.GetBindless().GetStats();
				const auto residency = renderer.GetResidency().GetStats();
				const auto geometry = renderer.GetGeometry().GetStats();
				const auto meshes = renderer.GetMeshes().GetStats();
				std::cout << "bindless textures " << bindless.LiveTextures << "/" << bindless.TextureCapacity << " (retiring "
						  << bindless.RetiringTextures << "), samplers " << bindless.LiveSamplers << "/" << bindless.SamplerCapacity << "\n"
						  << "residency: resident " << residency.Resident << ", bindless " << residency.BindlessTextures
						  << ", waiting for a slot " << residency.BindlessPending << ", queued " << residency.Queued << ", failed "
						  << residency.Failed << "\n"
						  << "geometry: resident " << geometry.ResidentMeshes << ", retiring " << geometry.RetiringMeshes << ", pending "
						  << geometry.PendingMeshes << "\n"
						  << "mesh library: meshes " << meshes.Meshes << " (resident " << meshes.ResidentMeshes << "), textures "
						  << meshes.Textures << " (resident " << meshes.ResidentTextures << "), pending releases " << meshes.PendingReleases
						  << ", replaced names " << meshes.Replacements << "\n"
						  << "materials " << renderer.GetMaterials().GetCount() << "\n";
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
	}

	void SwimEngine::RegisterCaptureCommands()
	{
		auto& commands = *commandRegistry;
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
	}

	void SwimEngine::RegisterProfilingCommands()
	{
		auto& commands = *commandRegistry;
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
				const std::filesystem::path csv =
					arguments.size() > 2 && arguments[2] != "-" ? std::filesystem::path(arguments[2]) : std::filesystem::path();
				const std::string label = arguments.size() > 3 ? arguments[3] : std::string();
				quitAfterProfile = arguments.size() > 4 && arguments[4] == "quit";
				profiler.Begin(warmup, frames, csv, label);
				std::cout << "[Profile] capturing " << frames << " frames after " << warmup << " warm-up frames\n";
			});
	}

	void SwimEngine::RegisterBenchCommands()
	{
		auto& commands = *commandRegistry;
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
				std::uint32_t ssrHalfCache = ssr.HalfTapCache ? 1u : 0u;
				std::uint32_t aoHalf = ao.HalfResolution ? 1u : 0u;
				std::uint32_t aoLinear = ao.LinearDepth ? 1u : 0u;
				std::uint32_t probesOn = probes.Enabled ? 1u : 0u;
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
					{ "ssr.half-cache", nullptr, &ssrHalfCache },
					{ "ao.half", nullptr, &aoHalf },
					{ "ao.linear", nullptr, &aoLinear },
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
					{ "probes.enabled", nullptr, &probesOn },
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
						ssr.HalfTapCache = ssrHalfCache != 0u;
						ao.HalfResolution = aoHalf != 0u;
						ao.LinearDepth = aoLinear != 0u;
						probes.Enabled = probesOn != 0u;
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
	}

	void SwimEngine::RegisterConsoleCommands()
	{
		auto& commands = *commandRegistry;
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

	namespace
	{
		// 0..100 -> look multiplier: 50 is 1, 13 is about 0.17, 100 about 4.1.
		float LookSensitivityScale(double value)
		{
			const double t = std::clamp(value, 0.0, 100.0) / 100.0 * 0.6 + 0.2;
			return static_cast<float>(t * t * t * 8.0);
		}
	} // namespace

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

		auto& sensitivity = commands.AddFloat("look_sensitivity", config.LookSensitivityDefault, 0.0, 100.0, "Mouse look speed (0 to 100)",
			"Controls/Mouse", VariableFlags::Saved);
		auto& invert = commands.AddBool("mouse_invert_y", false, "Inverts vertical mouse look", "Controls/Mouse", VariableFlags::Saved);
		actions.SetMouseSensitivity(LookSensitivityScale(sensitivity.GetFloat()));
		actions.SetLookSetting(sensitivity.GetFloat());
		actions.SetInvertMouseY(invert.GetBool());
		sensitivity.Subscribe(
			[this](const Swim::Commands::ConsoleVariable& variable)
			{
				actionMap->SetMouseSensitivity(LookSensitivityScale(variable.GetFloat()));
				actionMap->SetLookSetting(variable.GetFloat());
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

					std::cout << line << (actionMap->HasDefaultBindings(id) ? "" : " *") << "  [" << desc.Category << "] "
							  << desc.Description << '\n';
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

} // namespace Engine
