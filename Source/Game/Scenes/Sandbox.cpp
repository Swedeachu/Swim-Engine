#include "Game/Scenes/Sandbox.h"
#include "Engine/Runtime/RenderToggles.h"

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

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace Game
{

	namespace
	{

		constexpr float Pi = 3.14159265358979f;
		constexpr Engine::EngineState AllStates = Engine::EngineState::Playing | Engine::EngineState::Paused | Engine::EngineState::Stopped;

		const glm::vec3 PhysicsCenter{ 0.0f, 0.0f, 10.0f };
		const glm::vec3 FountainCenter{ 0.0f, 0.0f, -12.0f };
		const glm::vec3 HallCenter{ 12.0f, 0.0f, -6.0f };
		// Sponza sits behind the playgrounds (its long axis along X), the swarm of coloured
		// lights inside its atrium.
		// Side of the square checkered ground plane and of its collider, in metres.
		constexpr float GroundSize = 130.0f;
		const glm::vec3 SponzaCenter{ 0.0f, 0.02f, -52.0f };
		constexpr float SponzaLength = 30.0f;
		constexpr std::uint32_t SwarmLightCount = 256;

		// Camera bookmarks: eye and target per playground.
		struct Bookmark
		{
			const char* Name;
			glm::vec3 Eye;
			glm::vec3 Target;
		};

		const std::array<Bookmark, 10> Bookmarks{ {
			{ "Overview", { 2.0f, 7.5f, 26.0f }, { -1.0f, 1.5f, 0.0f } },
			{ "PBR gallery", { -13.1f, 3.4f, 1.5f }, { -13.1f, 2.7f, -9.0f } },
			{ "Instance hall", { 12.0f, 9.0f, 9.0f }, { 12.0f, 0.5f, -6.0f } },
			{ "Physics", { 7.0f, 5.0f, 19.0f }, { 0.0f, 1.2f, 9.0f } },
			{ "Fountain", { 0.0f, 3.5f, -3.0f }, { 0.0f, 1.8f, -12.0f } },
			{ "Sponza atrium", { -9.5f, 1.8f, -52.2f }, { 8.0f, 5.5f, -52.2f } },
			{ "Sponza from above", { -4.0f, 19.0f, -52.2f }, { 3.0f, 0.5f, -52.2f } },
			// The reflection lab (Sandbox::GetReflectionLabCenter) and the chrome playground balls.
			{ "Reflection lab", { -22.0f, 1.7f, 21.5f }, { -22.0f, 0.6f, 16.0f } },
			{ "Chrome spheres", { 5.2f, 1.1f, 12.6f }, { 3.9f, 0.5f, 9.2f } },
			// Sandbox::GetBlackHoleHome, from the side its disk is tilted toward.
			{ "Black hole", { 24.0f, 6.2f, 31.0f }, { 24.0f, 5.0f, 18.0f } },
		} };

		glm::quat LookRotation(const glm::vec3& direction)
		{
			const glm::vec3 d = glm::normalize(direction);
			const glm::vec3 up = std::abs(d.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
			return glm::quatLookAt(d, up);
		}

		Swim::Render::ParticleCurve Curve(float from, float to)
		{
			Swim::Render::ParticleCurve curve;
			curve.KeyCount = 2;
			curve.Times = { 0.0f, 1.0f, 1.0f, 1.0f };
			curve.Values = { from, to, to, to };
			return curve;
		}

		Swim::Render::ParticleGradient Gradient(std::array<float, 4> from, std::array<float, 4> to)
		{
			Swim::Render::ParticleGradient gradient;
			gradient.KeyCount = 2;
			gradient.Times = { 0.0f, 1.0f, 1.0f, 1.0f };
			gradient.Colors = { from, to, to, to };
			return gradient;
		}

	} // namespace

	int Sandbox::Awake()
	{
		auto& tags = GetTagRegistry();

		for (const char* name : { "Game.PbrGallery", "Game.InstanceHall", "Game.PhysicsToy", "Game.Glass", "Game.Emissive", "Game.Tentacle",
				 "Game.Spawned", "Game.Sponza", "Game.SwarmLight" })
		{
			tags.Register(name);
		}

		// Console commands (usable from --exec for scripted runs and captures).
		if (auto* commands = GetServices().Commands; commands && !commandsRegistered)
		{
			commandsRegistered = true;
			const auto number = [](const std::vector<std::string>& arguments, float fallback)
			{
				try
				{
					return arguments.empty() ? fallback : std::stof(arguments.front());
				}
				catch (...)
				{
					return fallback;
				}
			};
			commands->Register("sandbox.drop",
				[this, number](const std::vector<std::string>& arguments)
				{
					SpawnBalls(static_cast<std::uint32_t>(std::clamp(number(arguments, 10.0f), 1.0f, 500.0f)));
				});
			commands->Register("sandbox.rain",
				[this, number](const std::vector<std::string>& arguments)
				{
					SetRainBalls(number(arguments, 1.0f) != 0.0f);
				});
			commands->Register("sandbox.fire",
				[this](const std::vector<std::string>&)
				{
					if (auto* shooter = GetShooter())
					{
						shooter->Fire();
					}

				});
			commands->Register("sandbox.tab",
				[this, number](const std::vector<std::string>& arguments)
				{
					RequestTab(static_cast<std::uint32_t>(std::clamp(number(arguments, 0.0f), 0.0f, float(SandboxTabCount - 1))));
				});
			// Camera presets: 0 off, 1 clean modern, 2 cinematic 35mm, 3 anamorphic, 4 vintage,
			// 5 neutral photoreal.
			commands->Register("sandbox.camera",
				[this, number](const std::vector<std::string>& arguments)
				{
					const auto preset = std::clamp(number(arguments, 0.0f), 0.0f, float(Engine::CameraPresetCount - 1));
					ApplyCameraPreset(static_cast<Engine::CameraPreset>(static_cast<std::uint32_t>(preset)));
				});
			// sandbox.blackhole <x> <y> <z> [gas density]: moves the black hole (like dragging
			// it) and optionally sets its gas density (0: lensing only, as the HUD slider).
			commands->Register("sandbox.blackhole",
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
			commands->Register("sandbox.dof",
				[this, number](const std::vector<std::string>& arguments)
				{
					// sandbox.dof <f-number> [focus metres, 0 = auto]; f-number 0 turns it off.
					if (depthOfField)
					{
						const float fNumber = number(arguments, 2.8f);
						depthOfField->Enabled = fNumber > 0.0f;
						depthOfField->Settings.FNumber = std::max(fNumber, 0.7f);
						depthOfField->Settings.FocusDistance =
							arguments.size() > 1 ? std::max(std::strtof(arguments[1].c_str(), nullptr), 0.0f) : 0.0f;
					}

				});
			commands->Register("sandbox.clouds",
				[this, number](const std::vector<std::string>& arguments)
				{
					if (clouds)
					{
						const float coverage = std::clamp(number(arguments, 0.4f), 0.0f, 1.0f);
						clouds->Enabled = coverage > 0.0f;
						clouds->Settings.Coverage = coverage;
					}

				});
			// Clouds in the environment cube (reflections, ambient light) on/off.
			commands->Register("sandbox.cloudenv",
				[this, number](const std::vector<std::string>& arguments)
				{
					if (clouds)
					{
						clouds->Settings.Environment = number(arguments, 1.0f) != 0.0f;
					}

				});
			// The reflection lab's floor: 0 checker, 1 green, 2 rainbow (animated), 3 removed.
			commands->Register("sandbox.labfloor",
				[this, number](const std::vector<std::string>& arguments)
				{
					if (labFloor)
					{
						labFloor->SetMode(static_cast<ReflectionLabFloor::Mode>(
							static_cast<std::uint32_t>(std::clamp(number(arguments, 0.0f), 0.0f, 3.0f))));
					}

				});
			// Local reflection probes on/off, and the reflection debug view (0 off, 1 sources, 2 probe age).
			commands->Register("sandbox.probes",
				[this, number](const std::vector<std::string>& arguments)
				{
					if (auto* render = GetRenderServices(); render && render->Settings)
					{
						render->Settings->ReflectionProbes.Enabled = number(arguments, 1.0f) != 0.0f;
					}

				});
			commands->Register("sandbox.planar",
				[this, number](const std::vector<std::string>& arguments)
				{
					if (auto* render = GetRenderServices(); render && render->Settings)
					{
						render->Settings->PlanarReflections.Enabled = number(arguments, 1.0f) != 0.0f;
					}

				});
			commands->Register("sandbox.reflectdebug",
				[this, number](const std::vector<std::string>& arguments)
				{
					if (auto* render = GetRenderServices(); render && render->Settings)
					{
						render->Settings->ScreenSpace.Reflections.Debug = static_cast<Swim::Render::ReflectionDebugView>(
							static_cast<std::uint32_t>(std::clamp(number(arguments, 0.0f), 0.0f, 2.0f)));
					}

				});
			// Screen-space reflection thickness from the back faces (1) or the constant (0).
			commands->Register("sandbox.ssrbackfaces",
				[this, number](const std::vector<std::string>& arguments)
				{
					if (auto* render = GetRenderServices(); render && render->Settings)
					{
						render->Settings->ScreenSpace.Reflections.BackFaces = number(arguments, 1.0f) != 0.0f;
					}

				});
			// Whether the clouds in the environment also change the ambient light.
			commands->Register("sandbox.cloudambient",
				[this, number](const std::vector<std::string>& arguments)
				{
					if (auto* render = GetRenderServices(); render && render->Settings)
					{
						render->Settings->EnvironmentFeatureAmbient = number(arguments, 1.0f) != 0.0f;
					}

				});
			commands->Register("sandbox.sunfx",
				[this, number](const std::vector<std::string>& arguments)
				{
					const bool on = number(arguments, 1.0f) != 0.0f;

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
			commands->Register("sandbox.ssr",
				[this, number](const std::vector<std::string>& arguments)
				{
					if (auto* render = GetRenderServices(); render && render->Settings)
					{
						render->Settings->ScreenSpace.Reflections.Enabled = number(arguments, 1.0f) != 0.0f;
					}

				});
			commands->Register("sandbox.ssrhalf",
				[this, number](const std::vector<std::string>& arguments)
				{
					if (auto* render = GetRenderServices(); render && render->Settings)
					{
						render->Settings->ScreenSpace.Reflections.HalfResolution = number(arguments, 1.0f) != 0.0f;
					}

				});
			commands->Register("sandbox.ssrhistory",
				[this, number](const std::vector<std::string>& arguments)
				{
					if (auto* render = GetRenderServices(); render && render->Settings)
					{
						render->Settings->ScreenSpace.Reflections.History = number(arguments, 1.0f) != 0.0f;
					}

				});
			// Exposure: sandbox.exposure 0 = manual (Manual EV100), 1 = automatic.
			commands->Register("sandbox.exposure",
				[this, number](const std::vector<std::string>& arguments)
				{
					if (auto* render = GetRenderServices(); render && render->Settings)
					{
						render->Settings->Post.Exposure.Mode =
							number(arguments, 1.0f) != 0.0f ? Swim::Render::ExposureMode::Automatic : Swim::Render::ExposureMode::Manual;
					}

				});
			commands->Register("sandbox.fog",
				[this, number](const std::vector<std::string>& arguments)
				{
					if (auto* render = GetRenderServices(); render && render->Settings)
					{
						render->Settings->ScreenSpace.Fog.Enabled = number(arguments, 1.0f) != 0.0f;
					}

				});
			commands->Register("sandbox.hud",
				[this, number](const std::vector<std::string>& arguments)
				{
					SetHudVisible(number(arguments, 1.0f) != 0.0f);
				});
			commands->Register("sandbox.view",
				[this, number](const std::vector<std::string>& arguments)
				{
					GoToBookmark(static_cast<std::uint32_t>(std::max(0.0f, number(arguments, 0.0f))));
				});
			commands->Register("sandbox.sun",
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

		return 0;
	}

	std::uint32_t Sandbox::GetBookmarkCount()
	{
		return static_cast<std::uint32_t>(Bookmarks.size());
	}

	const char* Sandbox::GetBookmarkName(std::uint32_t index)
	{
		return index < Bookmarks.size() ? Bookmarks[index].Name : "";
	}

	bool Sandbox::GoToBookmark(std::uint32_t index)
	{
		auto* cameras = GetCameraSystem();

		if (!cameras || index >= Bookmarks.size())
		{
			return false;
		}

		cameras->GetCamera().LookAt(Bookmarks[index].Eye, Bookmarks[index].Target);
		cameras->RequestCameraCut(); // No motion-vector smear across the jump.
		cameraPlaced = true;		 // Keep the view if the scene rebuilds.
		lastBookmark = index;
		return true;
	}

	bool Sandbox::IsUiCapturing() const
	{
		const auto* render = GetRenderServices();
		return render && render->Ui && render->Ui->IsCapturingInput();
	}

	BallShooter* Sandbox::GetShooter() const
	{
		return GetBehavior<BallShooter>(shooterEntity);
	}

	void Sandbox::LoadPalette()
	{
		palette = {};
		auto* render = GetRenderServices();

		if (!render || !render->HasRenderer())
		{
			return; // Headless (tests): entities without GPU meshes.
		}

		palette.HasRenderer = true;

		for (std::size_t i = 0; i < palette.Meshes.size(); ++i)
		{
			palette.Meshes[i] = render->Meshes->Get(static_cast<Engine::BuiltinMesh>(i));
		}

		palette.Ground = render->Meshes->Find("SandboxGround");

		if (!palette.Ground.IsValid())
		{
			palette.Ground =
				render->Meshes->Register("SandboxGround", Engine::ProceduralMeshes::MakePlane(GroundSize, 13, GroundSize * 0.5f));
		}

		auto checker = render->Meshes->FindTexture("SandboxChecker");

		if (!checker.IsValid())
		{
			// Warm coral-sand tiles (the tropical look).
			checker = render->Meshes->RegisterChecker("SandboxChecker", 256, 2, { 240, 222, 184, 255 }, { 212, 188, 146, 255 });
		}

		Engine::MaterialDesc ground;
		ground.Name = "Sandbox ground";
		ground.BaseColor = { 0.78f, 0.76f, 0.72f, 1.0f };
		ground.Roughness = 0.85f;
		ground.BaseColorTexture = checker;
		palette.GroundMaterial = render->Materials->GetOrCreate(ground);
	}

	std::uint32_t Sandbox::Mat(const std::string& name, const glm::vec3& color, float metallic, float roughness, const glm::vec3& emissive,
		bool transparent, float alpha)
	{
		auto* render = GetRenderServices();

		if (!render || !render->HasRenderer())
		{
			return 0;
		}

		return Material(*render->Materials, name, color, metallic, roughness, emissive,
			transparent ? Engine::MaterialBlend::Transparent : Engine::MaterialBlend::Opaque, alpha);
	}

	void Sandbox::ApplyTropicalLook(Engine::RenderSettings& settings)
	{
		// Clear midday: a deep blue zenith fading to a lighter blue horizon, blue all the way
		// down in the void below it, and a high, soft white sun. The sky is kept moderately
		// bright: the tone curve desaturates whatever sits far above the exposure (the floor
		// sets it), and a brighter sky turned the horizon and the haze grey. No colour grade tints
		// the frame; warmth comes only from the sun being a touch warmer than the sky.
		auto& sky = settings.Sky;
		sky.ZenithColor = { 0.10f, 0.36f, 1.10f };
		sky.HorizonColor = { 0.40f, 0.66f, 1.12f };
		// Lighting and the reflection fallback see the lit floor below the horizon
		// (SetSunAngles computes its colour); the visible void stays blue.
		sky.GroundFalloff = 24.0f; // The ground starts right under the horizon, like the floor does.
		settings.SkyBackgroundGround = std::array<float, 3>{ 0.26f, 0.54f, 1.10f }; // A clear sky blue.
		sky.SunSharpness = 600.0f; // A tight sun glow; the shafts and the lens flare carry the rest.
		sky.Intensity = 1.0f;
		settings.EnvironmentIntensity = 1.1f;
		settings.Ambient = { 0.06f, 0.064f, 0.07f };
		auto& post = settings.Post;
		post.ToneMap.Operator = Swim::Render::ToneMapper::PbrNeutral;
		post.Exposure.Compensation = 2.9f;
		post.Exposure.ManualEv100 = 1.8f;
		post.Grading.Temperature = 0.0f;
		post.Grading.Tint = 0.0f;
		post.Grading.Contrast = TropicalContrast;
		post.Grading.Saturation = TropicalSaturation;
		post.Grading.Slope = { 1.0f, 1.0f, 1.0f };
		post.Bloom.Intensity = 0.056f;
		post.Bloom.Threshold = 1.2f;
		// A thin, pale, sunlit haze on distant geometry (a cyan haze tinted the whole scene).
		auto& fog = settings.ScreenSpace.Fog;
		fog.Enabled = true;
		fog.Density = 0.0025f;
		fog.HeightFalloff = 0.05f;
		fog.BaseHeight = 0.0f;
		fog.Color = { 0.40f, 0.66f, 1.12f }; // The horizon's colour: distant haze and sky blend.
		fog.SunColor = { 0.55f, 0.52f, 0.46f };
		fog.StartDistance = 12.0f;
		fog.MaxDistance = 100.0f; // The sky and the void are fogged along this much of their rays: a light haze, not a grey band.
		// Screen-space reflections are on by default in the sandbox (chrome and glossy
		// props); the HUD's Rendering tab can turn them off.
		auto& reflections = settings.ScreenSpace.Reflections;
		reflections.Enabled = true;
		// Long rays: the floor seen in a chrome ball's rim is often 20-60 m away; with the
		// 20 m default those rays faded out and fell back to the environment, which showed
		// as a fuzzy inner "ball" of sky colour inside the reflection.
		reflections.MaxDistance = 70.0f;
		reflections.MaxSteps = 96; // Planar captures and probes cover the long mirror rays.
		// Checkerboard: half the rays, every pixel traced every second frame (half-rate
		// screen-space reflections). Mirrors and the chrome spheres' caps come from planar
		// captures, so SSR is mostly what those miss. `sandbox.ssrhalf 0` traces every pixel.
		reflections.HalfResolution = true;
		// Settles the checkerboard and the march's noise; the filter clips its history to this
		// frame's neighbourhood and leaves planar reflections alone, so nothing trails.
		reflections.Temporal = true;
		settings.ScreenSpace.AmbientOcclusion.HalfResolution = true; // Likewise the AO (TAA gathers the block).
		reflections.DistanceFade = 0.15f;
		reflections.EdgeFade = 0.05f; // Close up, most hits are near the screen edge.
		// Only used without the back-face depth (each surface's real thickness otherwise).
		reflections.Thickness = 0.1f;
		// Local reflection probes (chrome balls, the gallery, the reflection lab): up to 16 at
		// 256 x 256 a face, 32 faces a frame. A chrome ball's own probe is its whole reflection
		// - one source, captured from its centre and parallax-corrected with the stored
		// distances - instead of a planar cap in the middle and the probe around it, which
		// showed a passing object twice (two captures from different frames) and stepped
		// where the probe lagged. A probe's changed faces (a mover in view or just left, a
		// moving probe) are captured together in one frame, the balls large on screen every
		// frame and the rest at no less than about half rate; every probe captured in a frame
		// is filtered in that frame. 256 matches the old planar cap's sharpness on a ball a
		// few hundred pixels across.
		settings.ReflectionProbes.MaxProbes = 16;
		settings.ReflectionProbes.FacesPerFrame = 32; // The lab's changing faces peak near 28.
		settings.ReflectionProbes.Resolution = 256;
		// A moving probe (the orbiting ball's) is re-captured whenever it moved at all: at 5 cm
		// (the default) the orbiting ball's reflection updated every second frame.
		settings.ReflectionProbes.MoveThreshold = 0.001f;
		settings.ReflectionProbes.MoverRange = 10.0f; // Movers farther away are a few texels in a probe.
		settings.ReflectionProbes.CullDistance = 60.0f; // The lab's probes skip Sponza (70 m away).
		// Idle faces (nothing moving in view) refresh every 30 frames.
		settings.ReflectionProbes.IdleRefreshFrames = 30;
		// Planar reflections (the mirror cube's faces; chrome balls use their probe): full rate - every
		// visible capture re-renders every frame (nothing is reprojected from an older frame),
		// at one capture texel per two screen pixels (a quarter of the pixels), magnified
		// through the composite's Catmull-Rom filter and jittered with the camera so TAA
		// anti-aliases them. At this density the 512 atlas holds any capture up close.
		auto& planar = settings.PlanarReflections;
		planar.Enabled = true;
		planar.MaxPlanes = 6; // The mirror cube's faces (at most three face the camera) and spares.
		planar.AtlasResolution = 512;
		planar.CapturesPerFrame = 6;
		planar.MaxAgeSeconds = 0.0f;
		planar.ResolutionScale = 0.5f;
		planar.Supersample = 1;
		planar.CullDistance = 80.0f;
		// 32-pixel light clusters: shorter light lists for the 256-light swarm (measured: the
		// Forward+ pass -0.6 ms in the atrium; the mask pass is word-major and cheap).
		settings.ClusterTileSize = 32;
		// Sharper sun shadows: 4096-texel cascades in an 8192 atlas halve the texel size
		// (the last cascade, 16-70 m, went from 8 cm to 4 cm per texel), so the PCF edge
		// no longer shows the texel staircase through its blur at playground distances.
		settings.Shadow.AtlasSize = 8192;
		settings.Shadow.CascadeResolution = 4096;
	}

	void Sandbox::AddAtmosphereFeatures()
	{
		// Render features are plain objects the scene configures and hands to the renderer;
		// the renderer records them every frame (Engine/Systems/Renderer/Runtime/RenderFeature.h).
		clouds = std::make_shared<Engine::VolumetricClouds>();
		clouds->Settings.Coverage = 0.5f; // Plenty of trade-wind cumulus, still mostly blue sky.
		clouds->Settings.BottomAltitude = 420.0f;
		// Fade the layer out well before the horizon: grazing rays cross kilometres of cloud
		// and turned the band above the horizon slate grey.
		clouds->Settings.MaxDistance = 9000.0f;
		clouds->Settings.HorizonFade = 0.3f;
		clouds->Settings.SunIntensity = 1.25f;	 // Bright white midday cumulus,
		clouds->Settings.AmbientStrength = 2.2f; // with light blue-grey (not storm grey) undersides.
		clouds->Settings.TopAltitude = 1250.0f;
		// 15 % faster than the original { 9, 0, 3.5 } m/s drift.
		clouds->Settings.Wind = { 9.0f * 1.15f, 0.0f, 3.5f * 1.15f };
		// A third of the pixels and 40 dithered steps (TAA resolves the dither): the march was
		// the costliest pass in open views (1.6 ms at 1080p on an RTX 4070 laptop).
		clouds->Settings.ResolutionScale = 0.375f;
		clouds->Settings.Steps = 40;
		sunShafts = std::make_shared<Engine::SunShafts>();
		lensFlare = std::make_shared<Engine::LensFlare>();
		// The camera: all three start disabled (the Off preset); the Camera/Post tab or
		// "sandbox.camera" turns them on.
		depthOfField = std::make_shared<Engine::DepthOfField>();
		cameraLens = std::make_shared<Engine::CameraLens>();
		filmSensor = std::make_shared<Engine::FilmSensor>();
		lensing = std::make_shared<Engine::GravitationalLensing>();
		auto* render = GetRenderServices();

		if (render && render->Renderer)
		{
			render->Renderer->AddFeature(clouds);
			render->Renderer->AddFeature(sunShafts);
			render->Renderer->AddFeature(lensFlare);
			render->Renderer->AddFeature(depthOfField);
			render->Renderer->AddFeature(cameraLens);
			render->Renderer->AddFeature(filmSensor);
			render->Renderer->AddFeature(lensing);
		}
	}

	void Sandbox::ApplyCameraPreset(Engine::CameraPreset preset)
	{
		cameraPreset = preset;
		const auto derived = Engine::DeriveCameraLook(Engine::CameraPresetLook(preset));
		Swim::Render::ColorGradingSettings scratch;
		auto* render = GetRenderServices();
		auto& grading = render && render->Settings ? render->Settings->Post.Grading : scratch;
		Engine::ApplyCameraLook(derived, depthOfField.get(), cameraLens.get(), filmSensor.get(), grading);
		// The tropical look's contrast and saturation are the neutral point the camera scales.
		grading.Contrast = TropicalContrast * derived.Contrast;
		grading.Saturation = TropicalSaturation * derived.Saturation;
	}

	int Sandbox::Init()
	{
		if (++builds == 1)
		{
			if (auto* render = GetRenderServices(); render && render->Settings)
			{
				ApplyTropicalLook(*render->Settings);
			}

			AddAtmosphereFeatures();
		}

		impacts = 0;
		strongestImpact = 0.0f;
		rainTimer = 0.0f;
		random.seed(1234u);
		LoadPalette();
		BuildCamera();
		BuildGround();
		BuildLighting();
		BuildPbrGallery();
		BuildInstanceHall();
		BuildGlassAndEmissive();
		BuildPhysicsPlayground();
		BuildParticles();
		BuildTentacles();
		BuildSponza();
		BuildLightSwarm();
		BuildReflectionLab();
		BuildBlackHole();
		BuildWorldUi();
		RegisterProfilingToggles();
		BuildHud();
		return 0;
	}

	void Sandbox::SetGroupShown(Engine::TagId tag, bool shown)
	{
		using Flags = Swim::Render::RenderObjectFlags;
		auto& registry = GetRegistry();

		for (const entt::entity entity : GetEntitiesWithTag(tag))
		{
			if (auto* mesh = registry.try_get<Engine::MeshRenderer>(entity))
			{
				const bool visible = (static_cast<std::uint32_t>(mesh->Flags) & static_cast<std::uint32_t>(Flags::Visible)) != 0;

				if (visible != shown)
				{
					registry.patch<Engine::MeshRenderer>(entity,
						[shown](Engine::MeshRenderer& renderer)
						{
							renderer.Flags = shown ? (renderer.Flags | Flags::Visible)
												   : static_cast<Flags>(static_cast<std::uint32_t>(renderer.Flags) &
														 ~static_cast<std::uint32_t>(Flags::Visible));
						});
				}
			}

			if (auto* light = registry.try_get<Engine::Light>(entity))
			{
				light->Enabled = shown;
			}
		}

		hiddenGroups[tag.Value] = !shown;
	}

	bool Sandbox::IsGroupShown(Engine::TagId tag) const
	{
		const auto it = hiddenGroups.find(tag.Value);
		return it == hiddenGroups.end() || !it->second;
	}

	void Sandbox::RegisterProfilingToggles()
	{
		// scene.* switches (render.toggle, the panel's Profiling section): hide a part of the
		// sandbox - its meshes, and its lights - to measure what it costs.
		auto* render = GetRenderServices();

		if (!render || !render->Toggles)
		{
			return;
		}

		hiddenGroups.clear();
		const auto group = [&](const std::string& name, const std::string& description, Engine::TagId tag)
		{
			render->Toggles->Register({ "scene." + name, description,
				[this, tag]
				{
					return IsGroupShown(tag);
				},
				[this, tag](bool on)
				{
					SetGroupShown(tag, on);
				} });
		};
		group("sponza", "The Sponza model", GameTags::Sponza);
		group("light-swarm", "The 256 roaming point lights and their orbs", GameTags::SwarmLight);
		group("instance-hall", "The 576-object instance hall", GameTags::InstanceHall);
		group("pbr-gallery", "The PBR gallery spheres", GameTags::PbrGallery);
		group("physics-toys", "The physics playground's bodies (drawing only)", GameTags::PhysicsToy);
		group("glass", "The glass panes (transparent)", GameTags::Glass);
		group("emissive", "The emissive tori", GameTags::Emissive);
		group("tentacles", "The GPU-skinned tentacles (drawing)", GameTags::Tentacle);
		group("reflection-lab", "The reflection lab (its probes stay)", GameTags::ReflectionLab);
		render->Toggles->Register({ "scene.light-swarm-motion", "Moving the swarm lights (CPU behaviour)",
			[this]
			{
				return swarmController == entt::null || swarmMotion;
			},
			[this](bool on)
			{
				swarmMotion = on;

				if (swarmController != entt::null)
				{
					SetEnabledStates(swarmController, on ? Engine::EngineState::Playing : Engine::EngineState::None);
				}

			} });
	}

	int Sandbox::Exit()
	{
		if (auto* render = GetRenderServices(); render && render->Toggles)
		{
			render->Toggles->Unregister("scene.");
		}

		infoDocument.reset();
		infoBody = {};
		cameraRig = entt::null;
		shooterEntity = entt::null;
		sun = entt::null;
		swarmController = entt::null;
		sponza.Entities.clear();
		infoButton = {};
		labFloor = nullptr;
		blackHole = entt::null;
		return 0;
	}

	void Sandbox::OnStateChanged(Engine::EngineState previous, Engine::EngineState current)
	{
		(void)previous;

		if (current == Engine::EngineState::Stopped)
		{
			rainBalls = false;
		}
	}

	void Sandbox::BuildCamera()
	{
		if (auto* cameras = GetCameraSystem(); cameras && !cameraPlaced)
		{
			cameras->GetCamera().LookAt(Bookmarks[0].Eye, Bookmarks[0].Target);
			cameras->RequestCameraCut();
			cameraPlaced = true;
		}

		cameraRig = CreateEntity("Camera rig");
		AddComponent<Engine::Transform>(cameraRig, Engine::Transform());
		AddTag(cameraRig, Engine::Tags::Camera);
		auto* fly = EmplaceBehavior<Engine::FlyCameraController>(cameraRig);
		fly->SetInputGate(
			[this]
			{
				return IsUiCapturing();
			});
		// The camera flies in every state (its entity's mask); the shooter, on its own
		// entity, only while playing (the default mask).
		SetEnabledStates(cameraRig, AllStates);
		shooterEntity = CreateEntity("Ball shooter");
		auto* shooter = EmplaceBehavior<BallShooter>(shooterEntity);
		shooter->SetInputGate(
			[this]
			{
				return IsUiCapturing();
			});
	}

	void Sandbox::BuildGround()
	{
		SpawnMesh(*this,
			{ "Ground", palette.Ground, palette.GroundMaterial, glm::vec3(0.0f), glm::vec3(1.0f), glm::quat(1, 0, 0, 0),
				Swim::Render::RenderObjectFlags::Visible | Swim::Render::RenderObjectFlags::Static,
				{ Engine::Tags::World, Engine::Tags::Static } });
		const entt::entity collider = CreateEntity("Ground collider");
		AddComponent<Engine::Transform>(collider, Engine::Transform(glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(1.0f)));
		// The collider covers the whole visible plane (it used to stop 25 m short of each edge).
		AddBoxBody(*this, collider, Engine::RigidbodyType::Static, { GroundSize * 0.5f, 0.5f, GroundSize * 0.5f });
		AddTag(collider, Engine::Tags::Static);
	}

	void Sandbox::BuildSponza()
	{
		// Default atrium volume when the model is unavailable (headless tests, no asset).
		swarmMin = SponzaCenter + glm::vec3(-10.5f, 0.4f, -4.2f);
		swarmMax = SponzaCenter + glm::vec3(9.9f, 9.4f, 3.9f);
		auto* render = GetRenderServices();
		auto* assets = GetServices().Assets;

		if (!render || !render->HasRenderer() || !assets)
		{
			return;
		}

		const std::vector<Engine::TagId> tags{ GameTags::Sponza, Engine::Tags::Static, Engine::Tags::Environment };

		if (!sponza.Valid() && !sponzaSearched)
		{
			sponzaSearched = true;
			// The optimized GLB (Draco meshes, KTX2/Basis textures transcoded by the cooker).
			const auto model = Engine::FindCookedModel(*assets, { "sponza" }, { "sponza-ktx-draco", "sponza-ktx", "gltf/sponza" });

			if (!model.IsValid())
			{
				std::cout << "[Sandbox] No cooked Sponza found. Put sponza-ktx-draco.glb (or sponza-ktx.glb) in "
							 "Assets/Models/Sponza/; it is cooked on the next start. Cooked models loaded:";
				std::size_t models = 0;

				for (const auto& entry : assets->GetDatabase().Snapshot())
				{
					if (entry.LogicalPath.size() > 6 && entry.LogicalPath.ends_with(".model"))
					{
						std::cout << ' ' << entry.LogicalPath;
						++models;
					}
				}

				std::cout << (models ? "\n" : " none (see the [Assets] lines above for cook errors).\n");
				return;
			}

			Engine::ModelPlacement placement;
			placement.Position = SponzaCenter;
			placement.TargetLength = SponzaLength;
			placement.Tags = tags;
			sponza = SpawnCookedModel(*this, *render, *assets, model, "Sponza", placement);

			if (!sponza.Valid())
			{
				std::cout << "[Sandbox] The cooked Sponza has no usable geometry.\n";
				return;
			}

			std::cout << "[Sandbox] Sponza: " << sponza.Parts.size() << " material groups, " << sponza.Triangles << " triangles, "
					  << sponza.Textures << " textures.\n";
		}
		else if (sponza.Valid())
		{
			sponza.Entities = RespawnModel(*this, sponza, tags);
		}

		if (sponza.Valid())
		{
			// The lights roam the atrium and its arcades: the building's interior between the
			// end walls and the outer aisle walls (fractions of the Crytek Sponza's bounds,
			// whose balconies and drapes reach past the walls), up to below the roof line.
			const glm::vec3 size = sponza.BoundsMax - sponza.BoundsMin;
			swarmMin = sponza.BoundsMin + glm::vec3(size.x * 0.15f, 0.4f, size.z * 0.27f);
			swarmMax = sponza.BoundsMax - glm::vec3(size.x * 0.17f, size.y * 0.25f, size.z * 0.29f);
		}
	}

	void Sandbox::BuildLightSwarm()
	{
		const entt::entity controller = CreateEntity("Sponza light swarm");
		AddComponent<Engine::Transform>(controller, Engine::Transform());
		LightSwarm::Settings settings;
		settings.BoxMin = swarmMin;
		settings.BoxMax = swarmMax;
		settings.MinSpeed = 0.9f;
		settings.MaxSpeed = 3.2f;
		settings.Seed = 20260926u;
		auto* swarm = EmplaceBehavior<LightSwarm>(controller, settings);
		swarmController = controller;

		// Saturated colours around the hue wheel, one emissive orb material per hue bucket.
		constexpr std::uint32_t HueBuckets = 16;
		std::array<std::uint32_t, HueBuckets> orbMaterials{};
		std::array<glm::vec3, HueBuckets> hues{};

		for (std::uint32_t h = 0; h < HueBuckets; ++h)
		{
			const float hue = static_cast<float>(h) / static_cast<float>(HueBuckets);
			const glm::vec3 rgb = glm::clamp(
				glm::abs(glm::fract(glm::vec3(hue) + glm::vec3(1.0f, 2.0f / 3.0f, 1.0f / 3.0f)) * 6.0f - 3.0f) - 1.0f, 0.0f, 1.0f);
			hues[h] = glm::mix(glm::vec3(1.0f), rgb, 0.85f); // Mostly saturated, a touch of white.
			orbMaterials[h] = Mat("Swarm orb " + std::to_string(h), hues[h], 0.0f, 0.4f, hues[h] * 14.0f);
		}

		std::mt19937 colors(0xC0FFEEu);
		std::uniform_int_distribution<std::uint32_t> pick(0, HueBuckets - 1);
		std::uniform_real_distribution<float> unit(0.0f, 1.0f);

		for (std::uint32_t i = 0; i < SwarmLightCount; ++i)
		{
			const std::uint32_t h = pick(colors);
			const glm::vec3 start = swarmMin + glm::vec3(unit(colors), unit(colors), unit(colors)) * (swarmMax - swarmMin);
			const entt::entity light = SpawnMesh(*this,
				{ "Swarm light " + std::to_string(i + 1), palette.Mesh(Engine::BuiltinMesh::Sphere), orbMaterials[h], start,
					glm::vec3(0.09f), glm::quat(1, 0, 0, 0), Swim::Render::RenderObjectFlags::Visible,
					{ GameTags::SwarmLight, Engine::Tags::Light } });
			Engine::Light point;
			point.Kind = Engine::LightKind::Point;
			point.Color = hues[h];
			// Compact ranges keep each cluster's light list short (and the frame fast)
			// while the pools of colour still overlap.
			point.Intensity = 6.0f + 4.0f * unit(colors);
			point.Range = 2.8f + 1.4f * unit(colors);
			AddComponent<Engine::Light>(light, point);
			swarm->Add(light);
		}
	}

	void Sandbox::SetSunAngles(float elevationDegrees, float azimuthDegrees)
	{
		sunElevation = std::clamp(elevationDegrees, 2.0f, 89.0f);
		sunAzimuth = std::fmod(std::fmod(azimuthDegrees, 360.0f) + 360.0f, 360.0f);
		const float el = glm::radians(sunElevation);
		const float az = glm::radians(sunAzimuth);
		const glm::vec3 towardSun{ std::cos(el) * std::sin(az), std::sin(el), std::cos(el) * std::cos(az) };

		if (IsValid(sun))
		{
			if (auto* transform = GetRegistry().try_get<Engine::Transform>(sun))
			{
				transform->SetRotation(LookRotation(-towardSun));
			}
		}

		if (auto* render = GetRenderServices(); render && render->Settings)
		{
			render->Settings->Sky.SunDirection = { towardSun.x, towardSun.y, towardSun.z };
			render->Settings->ScreenSpace.Fog.SunDirection = { -towardSun.x, -towardSun.y, -towardSun.z };
			// A lower sun is warmer and dimmer; a high one is nearly white.
			const float warmth = 1.0f - std::clamp((sunElevation - 5.0f) / 40.0f, 0.0f, 1.0f);
			render->Settings->Sky.SunColor = { 16.0f, 15.2f - 4.5f * warmth, 13.6f - 8.0f * warmth };
		}

		const float sunIntensity = 9.0f * std::clamp(std::sin(el) * 1.6f, 0.15f, 1.0f);

		if (IsValid(sun))
		{
			if (auto* light = GetRegistry().try_get<Engine::Light>(sun))
			{
				light->Intensity = sunIntensity;
			}
		}
	}

	void Sandbox::BuildLighting()
	{
		sun = CreateEntity("Sun");
		AddComponent<Engine::Transform>(sun, Engine::Transform());
		Engine::Light light;
		light.Kind = Engine::LightKind::Directional;
		light.Color = { 1.0f, 0.97f, 0.92f }; // Soft white midday sun.
		light.Intensity = 2.5f;
		light.CastShadows = true;
		light.ShadowPriority = 10.0f;
		AddComponent<Engine::Light>(sun, light);
		AddTag(sun, Engine::Tags::Light);
		SetSunAngles(sunElevation, sunAzimuth);

		// A shadowed spot over the physics playground.
		const entt::entity spot = CreateEntity("Playground spot");
		AddComponent<Engine::Transform>(spot,
			Engine::Transform(PhysicsCenter + glm::vec3(3.0f, 9.0f, 3.0f), glm::vec3(1.0f), LookRotation(glm::vec3(-0.3f, -1.0f, -0.3f))));
		Engine::Light spotLight;
		spotLight.Kind = Engine::LightKind::Spot;
		spotLight.Color = { 0.85f, 0.9f, 1.0f };
		spotLight.Intensity = 180.0f;
		spotLight.Range = 25.0f;
		spotLight.InnerCone = 0.35f;
		spotLight.OuterCone = 0.62f;
		spotLight.CastShadows = true;
		spotLight.ShadowPriority = 5.0f;
		AddComponent<Engine::Light>(spot, spotLight);
		AddTag(spot, Engine::Tags::Light);

		// A shadowed point "lantern" by the glass and emission corner.
		const entt::entity lantern = SpawnMesh(*this,
			{ "Lantern", palette.Mesh(Engine::BuiltinMesh::Sphere),
				Mat("Lantern glow", { 1.0f, 0.8f, 0.5f }, 0.0f, 0.4f, { 12.0f, 8.0f, 3.0f }), { -9.0f, 2.6f, -1.0f }, glm::vec3(0.18f),
				glm::quat(1, 0, 0, 0), Swim::Render::RenderObjectFlags::Visible, { Engine::Tags::Light } });
		Engine::Light lanternLight;
		lanternLight.Kind = Engine::LightKind::Point;
		lanternLight.Color = { 1.0f, 0.72f, 0.42f };
		lanternLight.Intensity = 40.0f;
		lanternLight.Range = 12.0f;
		lanternLight.CastShadows = true;
		lanternLight.ShadowPriority = 3.0f;
		AddComponent<Engine::Light>(lantern, lanternLight);
		EmplaceBehavior<Bob>(lantern, 0.15f, 0.3f, 0.0f);

		// Coloured point lights orbiting the instance hall (clustered, unshadowed).
		const std::array<glm::vec3, 6> colors{ glm::vec3(1.0f, 0.25f, 0.2f), glm::vec3(0.25f, 1.0f, 0.35f), glm::vec3(0.25f, 0.45f, 1.0f),
			glm::vec3(1.0f, 0.85f, 0.2f), glm::vec3(0.9f, 0.3f, 1.0f), glm::vec3(0.2f, 1.0f, 1.0f) };

		for (int i = 0; i < 12; ++i)
		{
			const auto& color = colors[static_cast<std::size_t>(i) % colors.size()];
			const entt::entity orb = SpawnMesh(*this,
				{ "Orbiting light " + std::to_string(i + 1), palette.Mesh(Engine::BuiltinMesh::Sphere),
					Mat("Orb " + std::to_string(i % 6), color, 0.0f, 0.5f, color * 6.0f), HallCenter, glm::vec3(0.12f),
					glm::quat(1, 0, 0, 0), Swim::Render::RenderObjectFlags::Visible, { Engine::Tags::Light, Engine::Tags::Effect } });
			Engine::Light point;
			point.Kind = Engine::LightKind::Point;
			point.Color = color;
			point.Intensity = 6.0f;
			point.Range = 5.0f;
			AddComponent<Engine::Light>(orb, point);
			EmplaceBehavior<Orbit>(orb, HallCenter, 4.0f + 2.5f * static_cast<float>(i % 3), 1.2f, 0.35f + 0.1f * static_cast<float>(i % 4),
				static_cast<float>(i) * (2.0f * Pi / 12.0f));
		}
	}

	void Sandbox::BuildPbrGallery()
	{
		// Rows: metallic 0 .. 1 (bottom to top); columns: roughness 0.05 .. 1.
		constexpr int columns = 7;
		constexpr int rows = 4;
		const glm::vec3 base = SrgbColor(230, 190, 120);

		for (int r = 0; r < rows; ++r)
		{
			for (int c = 0; c < columns; ++c)
			{
				const float metallic = static_cast<float>(r) / static_cast<float>(rows - 1);
				const float roughness = 0.05f + 0.95f * static_cast<float>(c) / static_cast<float>(columns - 1);
				const std::string name = "PBR m" + std::to_string(r) + " r" + std::to_string(c);
				const glm::vec3 color = metallic > 0.5f ? base : glm::mix(SrgbColor(200, 40, 40), base, metallic * 2.0f);
				SpawnMesh(*this,
					{ name, palette.Mesh(Engine::BuiltinMesh::Sphere), Mat(name, color, metallic, roughness),
						{ -17.0f + 1.3f * static_cast<float>(c), 0.8f + 1.3f * static_cast<float>(r), -9.0f }, glm::vec3(1.0f),
						glm::quat(1, 0, 0, 0), Swim::Render::RenderObjectFlags::Default | Swim::Render::RenderObjectFlags::Static,
						{ GameTags::PbrGallery, Engine::Tags::Static } });
			}
		}

		// A backdrop wall.
		const entt::entity wall = SpawnMesh(*this,
			{ "Gallery wall", palette.Mesh(Engine::BuiltinMesh::Cube), Mat("Gallery wall", SrgbColor(60, 64, 72), 0.0f, 0.9f),
				{ -13.1f, 3.0f, -10.3f }, { 10.5f, 6.0f, 0.4f }, glm::quat(1, 0, 0, 0),
				Swim::Render::RenderObjectFlags::Default | Swim::Render::RenderObjectFlags::Static, { Engine::Tags::Static } });
		AddBoxBody(*this, wall, Engine::RigidbodyType::Static, glm::vec3(0.5f));
		// One static area probe for the whole gallery (28 spheres share it).
		const entt::entity probe = CreateEntity("PBR gallery probe");
		AddComponent<Engine::Transform>(probe, Engine::Transform(glm::vec3(-13.1f, 2.5f, -7.5f), glm::vec3(1.0f)));
		Engine::ReflectionProbe area;
		area.ObjectProbe = false;
		area.Dynamic = false;
		area.InfluenceRadius = 7.0f;
		area.BlendDistance = 2.0f;
		AddComponent<Engine::ReflectionProbe>(probe, area);
	}

	void Sandbox::BuildInstanceHall()
	{
		constexpr int side = 24;
		const std::array<std::uint32_t, 6> materials{ Mat("Hall slate", SrgbColor(70, 78, 92), 0.0f, 0.6f),
			Mat("Hall steel", SrgbColor(170, 175, 185), 1.0f, 0.35f), Mat("Hall copper", SrgbColor(215, 120, 80), 1.0f, 0.3f),
			Mat("Hall ivory", SrgbColor(225, 220, 205), 0.0f, 0.5f), Mat("Hall teal", SrgbColor(40, 150, 150), 0.0f, 0.4f),
			Mat("Hall graphite", SrgbColor(35, 36, 40), 0.0f, 0.25f) };

		for (int z = 0; z < side; ++z)
		{
			for (int x = 0; x < side; ++x)
			{
				const float fx = static_cast<float>(x) - (side - 1) * 0.5f;
				const float fz = static_cast<float>(z) - (side - 1) * 0.5f;
				const float height = 0.3f + 1.4f * (0.5f + 0.5f * std::sin(fx * 0.45f) * std::cos(fz * 0.38f));
				const std::uint32_t material = materials[static_cast<std::size_t>((x / 4 + z / 4) % 6)];
				const entt::entity cube = SpawnMesh(*this,
					{ "Hall cube", palette.Mesh(Engine::BuiltinMesh::Cube), material,
						HallCenter + glm::vec3(fx * 0.55f, height * 0.5f, fz * 0.55f), { 0.4f, height, 0.4f }, glm::quat(1, 0, 0, 0),
						Swim::Render::RenderObjectFlags::Default | Swim::Render::RenderObjectFlags::Static,
						{ GameTags::InstanceHall, Engine::Tags::Static } });

				if ((x * 7 + z * 3) % 29 == 0)
				{
					EmplaceBehavior<Spin>(cube, glm::vec3(0, 1, 0), 45.0f + 10.0f * static_cast<float>(x % 5));
				}
			}
		}
	}

	void Sandbox::BuildGlassAndEmissive()
	{
		const std::array<std::pair<const char*, glm::vec3>, 4> glass{ { { "Red glass", { 0.9f, 0.15f, 0.12f } },
			{ "Green glass", { 0.15f, 0.85f, 0.3f } }, { "Blue glass", { 0.15f, 0.35f, 0.95f } },
			{ "Amber glass", { 0.95f, 0.65f, 0.1f } } } };

		for (std::size_t i = 0; i < glass.size(); ++i)
		{
			SpawnMesh(*this,
				{ glass[i].first, palette.Mesh(Engine::BuiltinMesh::Cube),
					Mat(glass[i].first, glass[i].second, 0.0f, 0.08f, glm::vec3(0.0f), true, 0.35f),
					{ -13.0f + 1.9f * static_cast<float>(i), 1.4f, 2.0f + 0.6f * static_cast<float>(i) }, { 1.6f, 2.4f, 0.06f },
					glm::angleAxis(0.25f * static_cast<float>(i), glm::vec3(0, 1, 0)), Swim::Render::RenderObjectFlags::Visible,
					{ GameTags::Glass } });
		}

		const std::array<std::pair<const char*, glm::vec3>, 3> emissive{ { { "Ember ring", { 6.0f, 2.2f, 0.4f } },
			{ "Ice ring", { 0.6f, 3.5f, 7.0f } }, { "Violet ring", { 4.5f, 0.8f, 6.0f } } } };

		for (std::size_t i = 0; i < emissive.size(); ++i)
		{
			const entt::entity ring = SpawnMesh(*this,
				{ emissive[i].first, palette.Mesh(Engine::BuiltinMesh::Torus),
					Mat(emissive[i].first, glm::vec3(0.1f), 0.2f, 0.4f, emissive[i].second),
					{ -14.0f + 3.0f * static_cast<float>(i), 1.6f, 6.0f }, glm::vec3(1.4f), glm::angleAxis(Pi * 0.5f, glm::vec3(1, 0, 0)),
					Swim::Render::RenderObjectFlags::Default, { GameTags::Emissive, Engine::Tags::Effect } });
			EmplaceBehavior<Spin>(ring, glm::vec3(0.3f, 1.0f, 0.2f), 40.0f + 25.0f * static_cast<float>(i));
			EmplaceBehavior<Bob>(ring, 0.25f, 0.25f, static_cast<float>(i));
		}
	}

	void Sandbox::BuildPhysicsPlayground()
	{
		const std::uint32_t crate = Mat("Crate", SrgbColor(190, 140, 90), 0.0f, 0.7f);
		const std::uint32_t crateDark = Mat("Crate dark", SrgbColor(120, 90, 60), 0.0f, 0.75f);
		// A five-level box pyramid.
		constexpr float size = 0.8f;
		int index = 0;

		for (int level = 0; level < 5; ++level)
		{
			const int count = 5 - level;

			for (int i = 0; i < count; ++i)
			{
				const float x = (static_cast<float>(i) - static_cast<float>(count - 1) * 0.5f) * (size + 0.02f);
				const entt::entity box = SpawnMesh(*this,
					{ "Crate " + std::to_string(++index), palette.Mesh(Engine::BuiltinMesh::Cube), (level + i) % 2 ? crate : crateDark,
						PhysicsCenter + glm::vec3(x, size * 0.5f + static_cast<float>(level) * size, 0.0f), glm::vec3(size),
						glm::quat(1, 0, 0, 0), Swim::Render::RenderObjectFlags::Default, { GameTags::PhysicsToy, Engine::Tags::Dynamic } });
				AddBoxBody(*this, box, Engine::RigidbodyType::Dynamic, glm::vec3(0.5f), 4.0f);
			}
		}

		// A ramp and a few capsules and spheres to knock around.
		const glm::quat tilt = glm::angleAxis(glm::radians(-18.0f), glm::vec3(1, 0, 0));
		const entt::entity ramp = SpawnMesh(*this,
			{ "Ramp", palette.Mesh(Engine::BuiltinMesh::Cube), Mat("Ramp", SrgbColor(90, 100, 115), 0.2f, 0.5f),
				PhysicsCenter + glm::vec3(-5.5f, 1.0f, -1.0f), { 2.5f, 0.3f, 6.0f }, tilt, Swim::Render::RenderObjectFlags::Default,
				{ GameTags::PhysicsToy, Engine::Tags::Static } });
		AddBoxBody(*this, ramp, Engine::RigidbodyType::Static, glm::vec3(0.5f));

		for (int i = 0; i < 4; ++i)
		{
			const entt::entity capsule = SpawnMesh(*this,
				{ "Capsule " + std::to_string(i + 1), palette.Mesh(Engine::BuiltinMesh::Capsule),
					Mat("Capsule", SrgbColor(80, 170, 220), 0.0f, 0.3f),
					PhysicsCenter + glm::vec3(-5.5f, 3.0f + static_cast<float>(i), -3.0f + 0.4f * static_cast<float>(i)), glm::vec3(1.0f),
					glm::angleAxis(Pi * 0.5f, glm::vec3(0, 0, 1)), Swim::Render::RenderObjectFlags::Default,
					{ GameTags::PhysicsToy, Engine::Tags::Dynamic } });
			AddCapsuleBody(*this, capsule, Engine::RigidbodyType::Dynamic, 0.25f, 0.25f, 1.5f);
		}

		for (int i = 0; i < 6; ++i)
		{
			const entt::entity ball = SpawnMesh(*this,
				{ "Toy sphere " + std::to_string(i + 1), palette.Mesh(Engine::BuiltinMesh::Sphere),
					Mat("Toy sphere", SrgbColor(230, 230, 235), 1.0f, 0.03f),
					PhysicsCenter + glm::vec3(3.0f + 0.9f * static_cast<float>(i % 3), 0.45f, -1.5f + 1.2f * static_cast<float>(i / 3)),
					glm::vec3(0.9f), glm::quat(1, 0, 0, 0), Swim::Render::RenderObjectFlags::Default,
					{ GameTags::PhysicsToy, Engine::Tags::Dynamic } });
			AddSphereBody(*this, ball, Engine::RigidbodyType::Dynamic, 0.5f, 3.0f);
			// Chrome: the ball's own probe is its whole reflection - the floor, the other balls and
			// whatever is behind the camera (screen-space reflections alone cannot).
			AddComponent<Engine::ReflectionProbe>(ball, Engine::ReflectionProbe{});
		}
	}

	void Sandbox::BuildParticles()
	{
		using namespace Swim::Render;
		// A fountain of glowing droplets that bounce on the ground.
		{
			const entt::entity fountain = CreateEntity("Fountain particles");
			AddComponent<Engine::Transform>(fountain, Engine::Transform(FountainCenter + glm::vec3(0.0f, 0.9f, 0.0f), glm::vec3(1.0f)));
			Engine::ParticleEmitter emitter;
			auto& d = emitter.Desc;
			d.Capacity = 6000;
			d.Blend = ParticleBlendMode::Additive;
			d.Rate = 1400.0f;
			d.Shape = ParticleShape::Sphere;
			d.ShapeExtent = { 0.08f, 0.0f, 0.0f };
			d.Direction = { 0.0f, 1.0f, 0.0f };
			d.ConeAngle = 0.22f;
			d.SpeedMin = 5.5f;
			d.SpeedMax = 7.0f;
			d.LifetimeMin = 1.6f;
			d.LifetimeMax = 2.4f;
			d.SizeMin = 0.05f;
			d.SizeMax = 0.09f;
			d.Collision = true;
			d.GroundHeight = 0.02f;
			d.Restitution = 0.35f;
			d.Friction = 0.2f;
			d.SizeOverLife = Curve(1.0f, 0.4f);
			d.ColorOverLife = Gradient({ 0.7f, 1.0f, 2.2f, 1.0f }, { 0.2f, 0.5f, 1.4f, 0.0f });
			d.Seed = 7;
			AddComponent<Engine::ParticleEmitter>(fountain, emitter);
			AddTag(fountain, Engine::Tags::Effect);
		}
		// Slow smoke above the emissive rings (alpha blended, sorted).
		{
			const entt::entity smoke = CreateEntity("Smoke particles");
			AddComponent<Engine::Transform>(smoke, Engine::Transform(glm::vec3(-11.0f, 0.4f, 6.0f), glm::vec3(1.0f)));
			Engine::ParticleEmitter emitter;
			auto& d = emitter.Desc;
			d.Capacity = 512;
			d.Blend = ParticleBlendMode::AlphaBlend;
			d.Rate = 45.0f;
			d.Shape = ParticleShape::Box;
			d.ShapeExtent = { 3.5f, 0.2f, 0.6f };
			d.Direction = { 0.0f, 1.0f, 0.0f };
			d.ConeAngle = 0.35f;
			d.SpeedMin = 0.3f;
			d.SpeedMax = 0.7f;
			d.LifetimeMin = 4.0f;
			d.LifetimeMax = 7.0f;
			d.SizeMin = 0.7f;
			d.SizeMax = 1.3f;
			d.RotationMax = 6.28f;
			d.AngularVelocityMin = -0.4f;
			d.AngularVelocityMax = 0.4f;
			d.Gravity = { 0.0f, 0.25f, 0.0f };
			d.Drag = 0.25f;
			d.SizeOverLife = Curve(0.6f, 2.2f);
			d.ColorOverLife = Gradient({ 0.35f, 0.35f, 0.4f, 0.45f }, { 0.5f, 0.5f, 0.55f, 0.0f });
			d.Seed = 11;
			AddComponent<Engine::ParticleEmitter>(smoke, emitter);
			AddTag(smoke, Engine::Tags::Effect);
		}
		// Looping spark bursts from the fountain's rim.
		{
			const entt::entity sparks = CreateEntity("Spark particles");
			AddComponent<Engine::Transform>(sparks, Engine::Transform(FountainCenter + glm::vec3(0.0f, 0.6f, 0.0f), glm::vec3(1.0f)));
			EmplaceBehavior<Spin>(sparks, glm::vec3(0, 1, 0), 60.0f);
			Engine::ParticleEmitter emitter;
			auto& d = emitter.Desc;
			d.Capacity = 1024;
			d.Blend = ParticleBlendMode::Additive;
			d.Rate = 0.0f;
			d.Duration = 1.2f;
			d.Looping = true;
			d.Bursts = { { 0.0f, 160 }, { 0.6f, 90 } };
			d.Shape = ParticleShape::Point;
			d.Direction = { 1.0f, 1.2f, 0.0f };
			d.ConeAngle = 0.5f;
			d.SpeedMin = 3.0f;
			d.SpeedMax = 6.0f;
			d.LifetimeMin = 0.6f;
			d.LifetimeMax = 1.1f;
			d.SizeMin = 0.025f;
			d.SizeMax = 0.05f;
			d.Collision = true;
			d.Restitution = 0.5f;
			d.ColorOverLife = Gradient({ 6.0f, 3.0f, 0.8f, 1.0f }, { 2.0f, 0.3f, 0.05f, 0.0f });
			d.Seed = 3;
			AddComponent<Engine::ParticleEmitter>(sparks, emitter);
			AddTag(sparks, Engine::Tags::Effect);
		}
		// The fountain basin.
		SpawnMesh(*this,
			{ "Fountain basin", palette.Mesh(Engine::BuiltinMesh::Cylinder), Mat("Basin stone", SrgbColor(140, 135, 125), 0.0f, 0.8f),
				FountainCenter + glm::vec3(0.0f, 0.3f, 0.0f), { 1.6f, 0.6f, 1.6f }, glm::quat(1, 0, 0, 0),
				Swim::Render::RenderObjectFlags::Default | Swim::Render::RenderObjectFlags::Static, { Engine::Tags::Static } });
	}

	void Sandbox::BuildTentacles()
	{
		auto* render = GetRenderServices();
		constexpr std::uint32_t joints = 5;
		constexpr float height = 2.6f;

		if (render && render->Bridge && !render->Bridge->HasSkinnedMesh("Tentacle"))
		{
			render->Bridge->RegisterSkinnedMesh("Tentacle", Engine::ProceduralMeshes::MakeSkinnedColumn(0.22f, height, joints, 20, 5));
		}

		const std::uint32_t material = Mat("Tentacle", SrgbColor(170, 60, 120), 0.1f, 0.35f, glm::vec3(0.05f, 0.0f, 0.03f));

		for (int i = 0; i < 6; ++i)
		{
			const float angle = static_cast<float>(i) * (2.0f * Pi / 6.0f) + 0.3f;
			const entt::entity tentacle = CreateEntity("Tentacle " + std::to_string(i + 1));
			AddComponent<Engine::Transform>(tentacle,
				Engine::Transform(FountainCenter + glm::vec3(std::cos(angle) * 3.0f, 0.0f, std::sin(angle) * 3.0f), glm::vec3(1.0f)));
			Engine::SkinnedMeshRenderer skinned;
			skinned.Mesh = "Tentacle";
			skinned.MaterialSet = material;
			AddComponent<Engine::SkinnedMeshRenderer>(tentacle, skinned);
			AddTag(tentacle, GameTags::Tentacle);
			EmplaceBehavior<TentacleAnimator>(tentacle, joints, height, static_cast<float>(i) * 1.1f);
		}
	}

	void Sandbox::BuildWorldUi()
	{
		auto* render = GetRenderServices();

		if (!render || !render->Ui)
		{
			return;
		}

		auto& ui = *render->Ui;
		// The info panel: a world-space document facing the start camera.
		infoDocument = ui.CreateDocument();
		{
			using namespace Swim::UI;
			UiStyle rootStyle;
			rootStyle.Flow = UiFlow::Column;
			rootStyle.Padding = { 18, 14, 18, 14 };
			rootStyle.Gap = 6;
			rootStyle.Background = UiSrgbHex(0x0a0d13, 0.93f);
			rootStyle.CornerRadius = 14;
			rootStyle.BorderWidth = 3;
			rootStyle.BorderColor = UiSrgbHex(0x1f7aff);
			rootStyle.Width = UiLength::Percent(1.0f);
			rootStyle.Height = UiLength::Percent(1.0f);
			const auto frame = CreateStyledNode(*infoDocument, infoDocument->GetRoot(), rootStyle);
			const auto title = CreateLabel(*infoDocument, frame, ui.GetBoldFonts(), "Swim Engine sandbox", 30.0f);
			(void)title;
			UiStyle bodyStyle;
			bodyStyle.Width = UiLength::Percent(1.0f);
			bodyStyle.TextWrap = Swim::Text::TextWrap::Word;
			infoBody = CreateLabel(*infoDocument, frame, ui.GetFonts(), "Starting...", 20.0f, bodyStyle);
			const auto button = CreateButton(*infoDocument, frame, "Fire a ball from here");
			CreateTooltip(*infoDocument, button, "World-space UI takes the mouse through the camera ray.");
			const entt::entity panel = CreateEntity("Info panel");
			AddComponent<Engine::Transform>(panel,
				Engine::Transform(glm::vec3(4.5f, 3.4f, -3.0f), glm::vec3(1.0f), glm::angleAxis(glm::radians(-15.0f), glm::vec3(0, 1, 0))));
			Engine::UiCanvas canvas;
			canvas.Document = infoDocument;
			canvas.Mode = UiCanvasMode::WorldPanel;
			canvas.Size = { 560.0f, 300.0f };
			canvas.UnitsPerPixel = 0.0085f;
			canvas.BlocksPointer = true;
			AddComponent<Engine::UiCanvas>(panel, canvas);
			AddTag(panel, Engine::Tags::Ui);
			infoButton = button;
		}
		// Billboard zone labels.
		const std::array<std::pair<const char*, glm::vec3>, 5> zones{ { { "PBR gallery", { -13.1f, 6.7f, -9.0f } },
			{ "Instance hall  (576 objects)", HallCenter + glm::vec3(0.0f, 3.2f, 0.0f) },
			{ "Physics playground", PhysicsCenter + glm::vec3(0.0f, 5.2f, 0.0f) },
			{ "Particles + GPU skinning", FountainCenter + glm::vec3(0.0f, 4.4f, 0.0f) },
			{ "Glass + emission", { -10.5f, 4.0f, 4.0f } } } };

		for (const auto& [text, position] : zones)
		{
			using namespace Swim::UI;
			auto document = ui.CreateDocument();
			UiStyle style;
			style.Padding = { 14, 6, 14, 6 };
			style.Background = UiSrgbHex(0x07090d, 0.85f);
			style.CornerRadius = 10;
			style.AlignSelf = UiAlign::Center;
			const auto box = CreateStyledNode(*document, document->GetRoot(), style);
			CreateLabel(*document, box, ui.GetBoldFonts(), text, 26.0f);
			const entt::entity label = CreateEntity(std::string("Label: ") + text);
			AddComponent<Engine::Transform>(label, Engine::Transform(position, glm::vec3(1.0f)));
			Engine::UiCanvas canvas;
			canvas.Document = document;
			canvas.Mode = UiCanvasMode::Billboard;
			canvas.Size = { 460.0f, 60.0f };
			canvas.UnitsPerPixel = 0.009f;
			canvas.ConstantScreenSize = true;
			canvas.ScreenPixelsPerCanvasPixel = 0.65f;
			canvas.Interactive = false;
			canvas.FadeStart = 45.0f;
			canvas.FadeEnd = 70.0f;
			AddComponent<Engine::UiCanvas>(label, canvas);
			AddTag(label, Engine::Tags::Ui);
		}
	}

	void Sandbox::BuildHud()
	{
		auto* render = GetRenderServices();

		if (!render || !render->Ui)
		{
			return;
		}

		const entt::entity hud = CreateEntity("HUD");
		auto* behavior = EmplaceBehavior<SandboxHud>(hud);
		SetEnabledStates(hud, AllStates);
		(void)behavior;
		AddTag(hud, Engine::Tags::Ui);
	}

	void Sandbox::RecordImpact(float impulse)
	{
		++impacts;
		strongestImpact = std::max(strongestImpact, impulse);
	}

	void Sandbox::SpawnBallAt(const glm::vec3& position, const glm::vec3& velocity)
	{
		auto* render = GetRenderServices();
		const auto mesh = palette.Mesh(Engine::BuiltinMesh::Sphere);
		const std::uint32_t material = Mat("Rain ball", SrgbColor(250, 180, 70), 0.0f, 0.3f, glm::vec3(0.0f));
		(void)render;
		++spawned;
		GetCommandBuffer().Create(
			[position, velocity, mesh, material](Engine::Scene& owner, entt::entity ball)
			{
				owner.SetEntityName(ball, "Rain ball");
				owner.AddComponent<Engine::Transform>(ball, Engine::Transform(position, glm::vec3(0.5f)));
				Engine::MeshRenderer renderer;
				renderer.Parts.push_back({ mesh, material });
				owner.AddComponent<Engine::MeshRenderer>(ball, std::move(renderer));
				AddSphereBody(owner, ball, Engine::RigidbodyType::Dynamic, 0.5f, 2.0f);
				owner.GetRegistry().get<Engine::Rigidbody>(ball).SetInitialLinearVelocity(velocity);
				owner.AddTag(ball, Engine::Tags::Projectile);
				owner.AddTag(ball, GameTags::Spawned);
				owner.EmplaceBehavior<Lifetime>(ball, 10.0f);
				owner.EmplaceBehavior<Projectile>(ball);
			});
	}

	void Sandbox::SpawnBalls(std::uint32_t count)
	{
		std::uniform_real_distribution<float> spread(-2.5f, 2.5f);

		for (std::uint32_t i = 0; i < count; ++i)
		{
			SpawnBallAt(PhysicsCenter + glm::vec3(spread(random), 7.0f + 0.4f * static_cast<float>(i), spread(random)),
				glm::vec3(0.0f, -1.0f, 0.0f));
		}
	}

	entt::entity Sandbox::SpawnPrimitive(Engine::BuiltinMesh mesh)
	{
		glm::vec3 position = PhysicsCenter + glm::vec3(0.0f, 6.0f, 2.0f);

		if (const auto* cameras = GetCameraSystem())
		{
			const auto& camera = cameras->GetCamera();
			position = camera.GetPosition() + camera.GetForward() * 4.0f;
			position.y = std::max(position.y, 1.0f);
		}

		std::uniform_real_distribution<float> hue(0.0f, 1.0f);
		const float h = hue(random);
		const glm::vec3 color{ 0.5f + 0.5f * std::cos(6.2831f * h), 0.5f + 0.5f * std::cos(6.2831f * (h + 0.33f)),
			0.5f + 0.5f * std::cos(6.2831f * (h + 0.67f)) };
		const std::uint32_t material = Mat("Spawned " + std::to_string(spawned % 8), color, (spawned % 2) ? 0.0f : 1.0f, 0.35f);
		const entt::entity entity = SpawnMesh(*this,
			{ "Spawned " + std::string(mesh == Engine::BuiltinMesh::Cube ? "cube" : "shape"), palette.Mesh(mesh), material, position,
				glm::vec3(0.8f), glm::quat(1, 0, 0, 0), Swim::Render::RenderObjectFlags::Default,
				{ GameTags::Spawned, Engine::Tags::Dynamic } });

		switch (mesh)
		{
		case Engine::BuiltinMesh::Sphere:
			AddSphereBody(*this, entity, Engine::RigidbodyType::Dynamic, 0.5f, 2.0f);
			break;
		case Engine::BuiltinMesh::Capsule:
			AddCapsuleBody(*this, entity, Engine::RigidbodyType::Dynamic, 0.25f, 0.25f, 1.5f);
			break;
		default:
			AddBoxBody(*this, entity, Engine::RigidbodyType::Dynamic, glm::vec3(0.5f), 2.0f);
			break;
		}

		++spawned;
		return entity;
	}

	void Sandbox::Update(double dt)
	{
		if (!rainBalls || dt <= 0.0)
		{
			return;
		}

		rainTimer += static_cast<float>(dt);

		while (rainTimer >= 0.15f)
		{
			rainTimer -= 0.15f;
			SpawnBalls(1);
		}
	}

} // namespace Game
