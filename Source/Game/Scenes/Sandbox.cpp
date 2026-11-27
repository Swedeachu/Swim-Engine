#include "Game/Scenes/Sandbox.h"
#include "Game/Scenes/SandboxShared.h"
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

	using namespace SandboxDetail;

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
			RegisterEffectCommands(*commands);
			RegisterOutlineCommands(*commands);
			RegisterWaterCommands(*commands);
			RegisterSkyCommands(*commands);
			RegisterReflectionCommands(*commands);
			RegisterViewCommands(*commands);
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
		// GPU-culled geometry drawn through the feature geometry hooks (lit, shadowed).
		boxField = std::make_shared<BoxField>();
		outline = std::make_shared<Engine::Outline>();
		water = std::make_shared<WaterDemo>();
		water->Enabled = false;
		waveWater = std::make_shared<Engine::WaveWater>();
		waveWater->Enabled = false;
		ocean = std::make_shared<Engine::OceanSurface>();
		ocean->Enabled = false;
		cullDemo = std::make_shared<CullDemo>();
		cullDemo->Enabled = false;
		celestial = std::make_shared<Engine::CelestialSky>();
		celestial->Enabled = false;

		// The face image is a copy in the temporary folder, so editing it shows hot reload.
		{
			std::error_code error;
			const auto& files = GetFileSystem();
			const auto copy = files.GetTemporaryRoot() / "BoxFieldFace.png";
			std::filesystem::create_directories(files.GetTemporaryRoot(), error);

			if (!std::filesystem::exists(copy, error))
			{
				std::filesystem::copy_file(files.ResolveAssetPath("Textures/Mart/mart.png"), copy, error);
			}

			if (std::filesystem::exists(copy, error))
			{
				boxField->FaceImage = copy;
			}
		}
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
			render->Renderer->AddFeature(boxField);
			render->Renderer->AddFeature(outline);
			render->Renderer->AddFeature(water);
			render->Renderer->AddFeature(waveWater);
			render->Renderer->AddFeature(ocean);
			render->Renderer->AddFeature(cullDemo);
			render->Renderer->AddFeature(celestial);
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
		BuildAudioPlayground();
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
		// Below the horizon only the sky follows (night); the light stays just above it.
		sunElevation = std::clamp(elevationDegrees, -30.0f, 89.0f);
		sunAzimuth = std::fmod(std::fmod(azimuthDegrees, 360.0f) + 360.0f, 360.0f);
		const float el = glm::radians(sunElevation);
		const float az = glm::radians(sunAzimuth);
		const glm::vec3 towardSun{ std::cos(el) * std::sin(az), std::sin(el), std::cos(el) * std::cos(az) };
		const float lightEl = glm::radians(std::max(sunElevation, 2.0f));
		const glm::vec3 towardLight{ std::cos(lightEl) * std::sin(az), std::sin(lightEl), std::cos(lightEl) * std::cos(az) };

		if (IsValid(sun))
		{
			if (auto* transform = GetRegistry().try_get<Engine::Transform>(sun))
			{
				transform->SetRotation(LookRotation(-towardLight));
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

		const float sunIntensity = sunElevation < 0.0f ? 0.05f : 9.0f * std::clamp(std::sin(el) * 1.6f, 0.15f, 1.0f);

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

} // namespace Game
