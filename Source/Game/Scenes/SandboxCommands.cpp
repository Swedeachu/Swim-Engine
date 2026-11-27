#include "Game/Scenes/Sandbox.h"
#include "Engine/Runtime/RenderToggles.h"
#include "Engine/Platform/FileSystem.h"

#include "Engine/Commands/CommandRegistry.h"
#include "Engine/Components/CameraComponent.h"
#include "Engine/Components/Light.h"
#include "Engine/Components/MeshRenderer.h"
#include "Engine/Components/ParticleEmitter.h"
#include "Engine/Components/PlanarReflector.h"
#include "Engine/Components/ReflectionProbe.h"
#include "Game/Behaviors/BlackHole.h"
#include "Game/Behaviors/ReflectionLabFloor.h"
#include "Engine/Components/SkinnedMeshRenderer.h"
#include "Engine/Components/Transform.h"
#include "Engine/Components/UiCanvas.h"
#include "Engine/Runtime/UiRuntime.h"
#include "Engine/Systems/Camera/CameraSystem.h"
#include "Engine/Systems/Camera/FlyCameraController.h"
#include "Engine/Systems/Physics/RigidBody.h"
#include "Engine/Systems/Renderer/Runtime/FrameRenderer.h"
#include "Engine/Systems/Renderer/Runtime/ProceduralMeshes.h"
#include "Engine/Systems/Renderer/Runtime/RenderServices.h"
#include "Engine/Systems/Scene/RenderExtraction/Runtime/SceneRenderBridge.h"
#include "Engine/Systems/Scene/SceneCommandBuffer.h"
#include "Engine/Systems/UI/UiWidgets.h"
#include "Game/Behaviors/BallShooter.h"
#include "Engine/Assets/AssetSystem.h"
#include "Game/Behaviors/LightSwarm.h"
#include "Game/Behaviors/Motion.h"
#include "Game/Behaviors/TentacleAnimator.h"
#include "Engine/Systems/Scene/ModelImporter/ModelImport.h"
#include "Game/SandboxContent.h"
#include "Game/Ui/SandboxHud.h"
#include "Game/Ui/UiShowcase.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace Game
{

	namespace
	{

		constexpr Engine::EngineState AllStates = Engine::EngineState::Playing | Engine::EngineState::Paused | Engine::EngineState::Stopped;

		// The first argument as a number, or fallback when it is missing or not one.
		float CommandNumber(const std::vector<std::string>& arguments, float fallback)
		{
			try
			{
				return arguments.empty() ? fallback : std::stof(arguments.front());
			}
			catch (...)
			{
				return fallback;
			}
		}

	} // namespace

	// Effects: drops, rain, fire, the black hole, depth of field and clouds.
	void Sandbox::RegisterEffectCommands(Swim::Commands::CommandRegistry& commands)
	{
		commands.Register("sandbox.drop",
			[this](const std::vector<std::string>& arguments)
			{
				SpawnBalls(static_cast<std::uint32_t>(std::clamp(CommandNumber(arguments, 10.0f), 1.0f, 500.0f)));
			});
		commands.Register("sandbox.rain",
			[this](const std::vector<std::string>& arguments)
			{
				SetRainBalls(CommandNumber(arguments, 1.0f) != 0.0f);
			});
		commands.Register("sandbox.fire",
			[this](const std::vector<std::string>&)
			{
				if (auto* shooter = GetShooter())
				{
					shooter->Fire();
				}
			});
		commands.Register("sandbox.tab",
			[this](const std::vector<std::string>& arguments)
			{
				RequestTab(static_cast<std::uint32_t>(std::clamp(CommandNumber(arguments, 0.0f), 0.0f, float(SandboxTabCount - 1))));
			});
		// Camera presets: 0 off, 1 clean modern, 2 cinematic 35mm, 3 anamorphic, 4 vintage,
		// 5 neutral photoreal.
		commands.Register("sandbox.camera",
			[this](const std::vector<std::string>& arguments)
			{
				const auto preset = std::clamp(CommandNumber(arguments, 0.0f), 0.0f, float(Engine::CameraPresetCount - 1));
				ApplyCameraPreset(static_cast<Engine::CameraPreset>(static_cast<std::uint32_t>(preset)));
			});
		// sandbox.blackhole <x> <y> <z> [gas density]: moves the black hole (like dragging
		// it) and optionally sets its gas density (0: lensing only, as the HUD slider).
		commands.Register("sandbox.blackhole",
			[this](const std::vector<std::string>& arguments)
			{
				if (blackHole == entt::null || arguments.size() < 3)
				{
					return;
				}

				auto& transform = GetRegistry().get<Engine::Transform>(blackHole);
				transform.SetWorldPosition(
					GetRegistry(), glm::vec3(std::stof(arguments[0]), std::stof(arguments[1]), std::stof(arguments[2])));

				if (arguments.size() >= 4)
				{
					if (auto* hole = GetBehavior<Game::BlackHole>(blackHole))
					{
						hole->SetGasDensity(std::max(0.0f, std::stof(arguments[3])));
					}
				}
			});
		commands.Register("sandbox.dof",
			[this](const std::vector<std::string>& arguments)
			{
				// sandbox.dof <f-number> [focus metres, 0 = auto]; f-number 0 turns it off.
				if (depthOfField)
				{
					const float fNumber = CommandNumber(arguments, 2.8f);
					depthOfField->Enabled = fNumber > 0.0f;
					depthOfField->Settings.FNumber = std::max(fNumber, 0.7f);
					depthOfField->Settings.FocusDistance =
						arguments.size() > 1 ? std::max(std::strtof(arguments[1].c_str(), nullptr), 0.0f) : 0.0f;
				}
			});
		commands.Register("sandbox.clouds",
			[this](const std::vector<std::string>& arguments)
			{
				if (clouds)
				{
					const float coverage = std::clamp(CommandNumber(arguments, 0.4f), 0.0f, 1.0f);
					clouds->Enabled = coverage > 0.0f;
					clouds->Settings.Coverage = coverage;
				}
			});
	}

	// Outline and overlay looks.
	void Sandbox::RegisterOutlineCommands(Swim::Commands::CommandRegistry& commands)
	{
		// sandbox.outline <x y z> [width] [creases 0|1] [rim strength]: outlines a unit box and a
		// pyramid beside it (no arguments: removes them).
		commands.Register("sandbox.outline",
			[this](const std::vector<std::string>& arguments)
			{
				if (!outline)
				{
					return;
				}

				outline->Clear();

				if (arguments.size() < 3)
				{
					return;
				}

				auto at = [&](std::size_t i, float fallback)
				{
					return i < arguments.size() ? std::strtof(arguments[i].c_str(), nullptr) : fallback;
				};
				Engine::Outline::Object box;
				box.Boxes.push_back({});
				box.Transform = { 1, 0, 0, at(0, 0.0f), 0, 1, 0, at(1, 0.0f), 0, 0, 1, at(2, 0.0f) };
				box.Width = at(3, 2.0f);
				box.Creases = at(4, 0.0f) != 0.0f;
				box.Rim = { 0.4f, 0.8f, 1.0f, at(5, 0.0f) };
				box.HiddenOpacity = 0.3f;
				outline->Set(box);
				Engine::Outline::Object pyramid = box;
				pyramid.Boxes.clear();
				const std::array<float, 3> apex{ 0.5f, 1.2f, 0.5f };
				const std::array<std::array<float, 3>, 4> corners{ { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 }, { 0, 0, 1 } } };
				for (int i = 0; i < 4; ++i)
				{
					pyramid.Triangles.insert(pyramid.Triangles.end(), { corners[i], corners[(i + 1) % 4], apex });
				}
				pyramid.Triangles.insert(
					pyramid.Triangles.end(), { corners[0], corners[1], corners[2], corners[0], corners[2], corners[3] });
				pyramid.Transform[3] += 1.6f;
				pyramid.Color = { 1.0f, 0.85f, 0.2f, 1.0f };
				outline->Set(pyramid);
			});
		// sandbox.overlay <0|1> <x y z> <radius> [fov] [near]: puts every mesh within radius of the
		// point on the first-person overlay layer (or takes it off): drawn in front of the scene.
		commands.Register("sandbox.overlay",
			[this](const std::vector<std::string>& arguments)
			{
				using Flags = Swim::Render::RenderObjectFlags;
				auto at = [&](std::size_t i, float fallback)
				{
					return i < arguments.size() ? std::strtof(arguments[i].c_str(), nullptr) : fallback;
				};
				const bool on = at(0, 1.0f) != 0.0f;
				const glm::vec3 center{ at(1, 0.0f), at(2, 0.0f), at(3, 0.0f) };
				const float radius = at(4, 1.0f);
				auto& registry = GetRegistry();

				for (const auto entity : registry.view<Engine::MeshRenderer, Engine::Transform>())
				{
					if (glm::length(registry.get<Engine::Transform>(entity).GetPosition() - center) > radius)
					{
						continue;
					}

					registry.patch<Engine::MeshRenderer>(entity,
						[on](Engine::MeshRenderer& renderer)
						{
							const auto bits = static_cast<std::uint32_t>(renderer.Flags) & ~static_cast<std::uint32_t>(Flags::Overlay);
							renderer.Flags = static_cast<Flags>(bits | (on ? static_cast<std::uint32_t>(Flags::Overlay) : 0u));
						});
				}

				if (auto* render = GetRenderServices(); render && render->Settings)
				{
					render->Settings->Overlay.FieldOfView = at(5, render->Settings->Overlay.FieldOfView);
					render->Settings->Overlay.Near = at(6, render->Settings->Overlay.Near);
				}
			});
	}

	// Water, waves, ocean, floaters and water LOD.
	void Sandbox::RegisterWaterCommands(Swim::Commands::CommandRegistry& commands)
	{
		// sandbox.water <0|1> [x y z] [half size] [waves] [murk]: a water rectangle (Game::WaterDemo).
		commands.Register("sandbox.water",
			[this](const std::vector<std::string>& arguments)
			{
				if (!water)
				{
					return;
				}

				auto at = [&](std::size_t i, float fallback)
				{
					return i < arguments.size() ? std::strtof(arguments[i].c_str(), nullptr) : fallback;
				};
				water->Enabled = at(0, 1.0f) != 0.0f;
				water->Center = { at(1, water->Center[0]), at(2, water->Center[1]), at(3, water->Center[2]) };
				water->HalfSize = { at(4, water->HalfSize[0]), at(4, water->HalfSize[1]) };
				water->WaveHeight = at(5, water->WaveHeight);
				water->Murk = at(6, water->Murk);
			});
		// sandbox.waves <0|1> [x z] [half size, 0 = endless] [height] [amplitude]: wave water with
		// buoyancy (Engine::WaveWater + a PhysicsWorld fluid volume on the same waves).
		commands.Register("sandbox.waves",
			[this](const std::vector<std::string>& arguments)
			{
				auto at = [&](std::size_t i, float fallback)
				{
					return i < arguments.size() ? std::strtof(arguments[i].c_str(), nullptr) : fallback;
				};
				SetWaves(at(0, 1.0f) != 0.0f, { at(1, 0.0f), at(2, 0.0f) }, at(3, 0.0f), at(4, 1.5f), at(5, 0.3f));
			});
		// sandbox.ocean <0|1> [height] [wind m/s]: the FFT ocean with buoyancy on its GPU heights
		// (read back to the CPU, OceanSimulation::SampleSurface).
		commands.Register("sandbox.ocean",
			[this](const std::vector<std::string>& arguments)
			{
				auto at = [&](std::size_t i, float fallback)
				{
					return i < arguments.size() ? std::strtof(arguments[i].c_str(), nullptr) : fallback;
				};
				SetOcean(at(0, 1.0f) != 0.0f, at(1, 1.5f), at(2, 10.0f));
			});
		// sandbox.floaters [count] [x y z]: buoyant shapes thrown onto the wave water.
		commands.Register("sandbox.floaters",
			[this](const std::vector<std::string>& arguments)
			{
				auto at = [&](std::size_t i, float fallback)
				{
					return i < arguments.size() ? std::strtof(arguments[i].c_str(), nullptr) : fallback;
				};
				glm::vec3 around(0.0f, 4.0f, 0.0f);

				if (const auto* cameras = GetCameraSystem(); cameras && arguments.size() < 4)
				{
					const auto& camera = cameras->GetCamera();
					around = camera.GetPosition() + camera.GetForward() * 12.0f;
				}

				SpawnFloaters(static_cast<std::uint32_t>(std::clamp(at(0, 12.0f), 1.0f, 200.0f)),
					{ at(1, around.x), at(2, around.y), at(3, around.z) });
			});
		// sandbox.cutouts [far]: thin alpha-tested cards out to `far` metres.
		commands.Register("sandbox.cutouts",
			[this](const std::vector<std::string>& arguments)
			{
				SpawnCutouts(arguments.empty() ? 160.0f : std::clamp(std::strtof(arguments[0].c_str(), nullptr), 10.0f, 1000.0f));
			});
		// sandbox.waterlod <mode> [detail start end] [amplitude start end] [far amplitude]: 0 off,
		// 1 continuous (default distances or the given ones), 2 a hard cut at the detail start
		// (no far field), 3 continuous with the debug tint. Applies to wave water.
		commands.Register("sandbox.waterlod",
			[this](const std::vector<std::string>& arguments)
			{
				if (!waveWater)
				{
					return;
				}

				auto at = [&](std::size_t i, float fallback)
				{
					return i < arguments.size() ? std::strtof(arguments[i].c_str(), nullptr) : fallback;
				};
				const int mode = static_cast<int>(at(0, 1.0f));
				Engine::WaterLodSettings lod;
				lod.DetailStart = at(1, lod.DetailStart);
				lod.DetailEnd = std::max(at(2, lod.DetailEnd), lod.DetailStart);
				lod.AmplitudeStart = at(3, lod.AmplitudeStart);
				lod.AmplitudeEnd = std::max(at(4, lod.AmplitudeEnd), lod.AmplitudeStart);
				lod.FarAmplitude = std::clamp(at(5, lod.FarAmplitude), 0.0f, 1.0f);
				lod.Enabled = mode != 0;
				lod.Debug = mode == 3;
				waveWater->Settings.Extent = 600.0f;

				if (mode == 2)
				{
					// What a switch to "no water" at one distance (30 m unless given) looks like.
					lod.DetailStart = arguments.size() > 1 ? lod.DetailStart : 30.0f;
					lod.DetailEnd = lod.DetailStart;
					lod.AmplitudeStart = lod.AmplitudeEnd = lod.DetailStart;
					lod.FarAmplitude = 0.0f;
					lod.NormalStart = lod.NormalEnd = lod.DetailStart;
					lod.FarField = false;
					waveWater->Settings.Extent = lod.DetailStart;
				}

				waveWater->Settings.Lod = lod;
			});
	}

	// Culling, the celestial sky and cloud styles.
	void Sandbox::RegisterSkyCommands(Swim::Commands::CommandRegistry& commands)
	{
		// sandbox.cull <0|1> [x y z] [xray] [occlusion] [wall]: boxes behind a wall culled by
		// Engine::IndirectCuller (Game::CullDemo); xray shows the boxes the culler kept.
		commands.Register("sandbox.cull",
			[this](const std::vector<std::string>& arguments)
			{
				if (!cullDemo)
				{
					return;
				}

				auto at = [&](std::size_t i, float fallback)
				{
					return i < arguments.size() ? std::strtof(arguments[i].c_str(), nullptr) : fallback;
				};
				cullDemo->Enabled = at(0, 1.0f) != 0.0f;
				cullDemo->Origin = { at(1, cullDemo->Origin[0]), at(2, cullDemo->Origin[1]), at(3, cullDemo->Origin[2]) };
				cullDemo->Xray = at(4, cullDemo->Xray ? 1.0f : 0.0f) != 0.0f;
				cullDemo->Occlusion = at(5, cullDemo->Occlusion ? 1.0f : 0.0f) != 0.0f;
				cullDemo->Wall = at(6, cullDemo->Wall ? 1.0f : 0.0f) != 0.0f;
			});
		// sandbox.celestial <0|1> [moon phase] [sidereal angle] [visibility] [star brightness] [sky intensity] [moon size]
		commands.Register("sandbox.celestial",
			[this](const std::vector<std::string>& arguments)
			{
				if (!celestial)
				{
					return;
				}

				auto at = [&](std::size_t i, float fallback)
				{
					return i < arguments.size() ? std::strtof(arguments[i].c_str(), nullptr) : fallback;
				};
				celestial->Enabled = at(0, 1.0f) != 0.0f;
				celestial->Settings.MoonPhase = at(1, celestial->Settings.MoonPhase);
				celestial->Settings.SiderealAngle = at(2, celestial->Settings.SiderealAngle);
				celestial->Settings.Visibility = at(3, celestial->Settings.Visibility);
				celestial->Settings.StarBrightness = at(4, celestial->Settings.StarBrightness);

				// [sky intensity]: a dark night sky to see the stars against.
				if (auto* render = GetRenderServices(); render && render->Settings && arguments.size() > 5)
				{
					render->Settings->Sky.Intensity = at(5, 1.0f);
					render->Settings->EnvironmentIntensity = 1.1f * at(5, 1.0f);
				}

				celestial->Settings.MoonSize = at(6, celestial->Settings.MoonSize);
			});
		// sandbox.cloudstyle <0 cumulus | 1 stratocumulus | 2 stratus | 3 cumulonimbus> [bottom top] [cirrus]
		commands.Register("sandbox.cloudstyle",
			[this](const std::vector<std::string>& arguments)
			{
				if (!clouds)
				{
					return;
				}

				auto at = [&](std::size_t i, float fallback)
				{
					return i < arguments.size() ? std::strtof(arguments[i].c_str(), nullptr) : fallback;
				};
				clouds->ApplyStyle(static_cast<Engine::CloudStyle>(std::clamp(static_cast<int>(at(0, 0.0f)), 0, 3)));
				clouds->Settings.BottomAltitude = at(1, clouds->Settings.BottomAltitude);
				clouds->Settings.TopAltitude = at(2, clouds->Settings.TopAltitude);
				clouds->Settings.CirrusCoverage = at(3, clouds->Settings.CirrusCoverage);
			});
		commands.Register("sandbox.boxfield",
			[this](const std::vector<std::string>& arguments)
			{
				if (boxField)
				{
					boxField->Enabled = CommandNumber(arguments, 1.0f) != 0.0f;
				}
			});
		// Clouds in the environment cube (reflections, ambient light) on/off.
		commands.Register("sandbox.cloudenv",
			[this](const std::vector<std::string>& arguments)
			{
				if (clouds)
				{
					clouds->Settings.Environment = CommandNumber(arguments, 1.0f) != 0.0f;
				}
			});
	}

	// The lab floor, probes, planar reflections and SSR.
	void Sandbox::RegisterReflectionCommands(Swim::Commands::CommandRegistry& commands)
	{
		// The reflection lab's floor: 0 checker, 1 green, 2 rainbow (animated), 3 removed.
		commands.Register("sandbox.labfloor",
			[this](const std::vector<std::string>& arguments)
			{
				if (labFloor)
				{
					labFloor->SetMode(static_cast<ReflectionLabFloor::Mode>(
						static_cast<std::uint32_t>(std::clamp(CommandNumber(arguments, 0.0f), 0.0f, 3.0f))));
				}
			});
		// Local reflection probes on/off, and the reflection debug view (0 off, 1 sources, 2 probe age).
		commands.Register("sandbox.probes",
			[this](const std::vector<std::string>& arguments)
			{
				if (auto* render = GetRenderServices(); render && render->Settings)
				{
					render->Settings->ReflectionProbes.Enabled = CommandNumber(arguments, 1.0f) != 0.0f;
				}
			});
		commands.Register("sandbox.planar",
			[this](const std::vector<std::string>& arguments)
			{
				if (auto* render = GetRenderServices(); render && render->Settings)
				{
					render->Settings->PlanarReflections.Enabled = CommandNumber(arguments, 1.0f) != 0.0f;
				}
			});
		commands.Register("sandbox.reflectdebug",
			[this](const std::vector<std::string>& arguments)
			{
				if (auto* render = GetRenderServices(); render && render->Settings)
				{
					render->Settings->ScreenSpace.Reflections.Debug = static_cast<Swim::Render::ReflectionDebugView>(
						static_cast<std::uint32_t>(std::clamp(CommandNumber(arguments, 0.0f), 0.0f, 2.0f)));
				}
			});
		// Screen-space reflection thickness from the back faces (1) or the constant (0).
		commands.Register("sandbox.ssrbackfaces",
			[this](const std::vector<std::string>& arguments)
			{
				if (auto* render = GetRenderServices(); render && render->Settings)
				{
					render->Settings->ScreenSpace.Reflections.BackFaces = CommandNumber(arguments, 1.0f) != 0.0f;
				}
			});
		// Whether the clouds in the environment also change the ambient light.
		commands.Register("sandbox.cloudambient",
			[this](const std::vector<std::string>& arguments)
			{
				if (auto* render = GetRenderServices(); render && render->Settings)
				{
					render->Settings->EnvironmentFeatureAmbient = CommandNumber(arguments, 1.0f) != 0.0f;
				}
			});
		commands.Register("sandbox.sunfx",
			[this](const std::vector<std::string>& arguments)
			{
				const bool on = CommandNumber(arguments, 1.0f) != 0.0f;

				if (sunShafts)
				{
					sunShafts->Enabled = on;
				}

				if (lensFlare)
				{
					lensFlare->Enabled = on;
				}
			});
		// Screen-space reflections and height fog on/off (A/B comparisons from the command line).
		commands.Register("sandbox.ssr",
			[this](const std::vector<std::string>& arguments)
			{
				if (auto* render = GetRenderServices(); render && render->Settings)
				{
					render->Settings->ScreenSpace.Reflections.Enabled = CommandNumber(arguments, 1.0f) != 0.0f;
				}
			});
		commands.Register("sandbox.ssrhalf",
			[this](const std::vector<std::string>& arguments)
			{
				if (auto* render = GetRenderServices(); render && render->Settings)
				{
					render->Settings->ScreenSpace.Reflections.HalfResolution = CommandNumber(arguments, 1.0f) != 0.0f;
				}
			});
		commands.Register("sandbox.ssrhistory",
			[this](const std::vector<std::string>& arguments)
			{
				if (auto* render = GetRenderServices(); render && render->Settings)
				{
					render->Settings->ScreenSpace.Reflections.History = CommandNumber(arguments, 1.0f) != 0.0f;
				}
			});
	}

	// Exposure, fog, zones, the HUD, the showcase, views and the sun.
	void Sandbox::RegisterViewCommands(Swim::Commands::CommandRegistry& commands)
	{
		// Exposure: sandbox.exposure 0 = manual (Manual EV100), 1 = automatic.
		commands.Register("sandbox.exposure",
			[this](const std::vector<std::string>& arguments)
			{
				if (auto* render = GetRenderServices(); render && render->Settings)
				{
					render->Settings->Post.Exposure.Mode =
						CommandNumber(arguments, 1.0f) != 0.0f ? Swim::Render::ExposureMode::Automatic : Swim::Render::ExposureMode::Manual;
				}
			});
		commands.Register("sandbox.fog",
			[this](const std::vector<std::string>& arguments)
			{
				if (auto* render = GetRenderServices(); render && render->Settings)
				{
					render->Settings->ScreenSpace.Fog.Enabled = CommandNumber(arguments, 1.0f) != 0.0f;
				}
			});
		commands.Register("sandbox.zone",
			[this](const std::vector<std::string>&)
			{
				std::cout << "[Sandbox] Balls in the sound zone: " << zoneOccupants << ", scene timers: " << GetTimers().GetActiveCount()
						  << '\n';
			});
		commands.Register("sandbox.hud",
			[this](const std::vector<std::string>& arguments)
			{
				SetHudVisible(CommandNumber(arguments, 1.0f) != 0.0f);
			});
		commands.Register("sandbox.showcase",
			[this](const std::vector<std::string>& arguments)
			{
				const bool on = CommandNumber(arguments, 1.0f) != 0.0f;

				if (on && showcase == entt::null)
				{
					showcase = CreateEntity("UI showcase");
					EmplaceBehavior<UiShowcase>(showcase);
					SetEnabledStates(showcase, AllStates);
				}
				else if (!on && showcase != entt::null)
				{
					DestroyEntity(showcase);
					showcase = entt::null;
				}
			});
		commands.Register("sandbox.view",
			[this](const std::vector<std::string>& arguments)
			{
				GoToBookmark(static_cast<std::uint32_t>(std::max(0.0f, CommandNumber(arguments, 0.0f))));
			});
		commands.Register("sandbox.sun",
			[this](const std::vector<std::string>& arguments)
			{
				try
				{
					SetSunAngles(arguments.size() > 0 ? std::stof(arguments[0]) : sunElevation,
						arguments.size() > 1 ? std::stof(arguments[1]) : sunAzimuth);
				}
				catch (...)
				{
				}
			});
	}

} // namespace Game
