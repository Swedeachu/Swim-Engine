#include "Engine/Components/CameraComponent.h"
#include "Engine/Components/Light.h"
#include "Engine/Components/ParticleEmitter.h"
#include "Engine/Components/SkinnedMeshRenderer.h"
#include "Engine/Components/Transform.h"
#include "Engine/Components/UiCanvas.h"
#include "Engine/Systems/Camera/CameraSystem.h"
#include "Engine/Systems/Scene/SceneSystem.h"
#include "Game/Behaviors/BallShooter.h"
#include "Game/Behaviors/BlackHole.h"
#include "Engine/Components/Pickable.h"
#include "Game/Behaviors/LightSwarm.h"
#include "Game/Behaviors/ReflectionLabFloor.h"
#include "Engine/Components/PlanarReflector.h"
#include "Engine/Components/ReflectionProbe.h"
#include "Engine/Systems/Renderer/Runtime/RenderServices.h"
#include "Engine/Systems/Renderer/Runtime/RenderSettings.h"
#include "Engine/Components/MeshRenderer.h"
#include "Engine/Assets/AssetSystem.h"
#include "Engine/Components/Light.h"
#include "Game/Game.h"
#include "Game/SandboxContent.h"
#include "Game/Scenes/Sandbox.h"
#include "Game/Ui/SandboxHud.h"
#include "Engine/Runtime/UiRuntime.h"
#include "Engine/Systems/UI/UiWidgets.h"
#include "Tests/Framework/Test.h"
#include "Tests/Suites/Engine/HeadlessEngine.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <vector>
#include <string>

using Engine::EngineState;

namespace
{

	Swim::Tests::HeadlessEngine MakeSandbox()
	{
		return Swim::Tests::HeadlessEngine(
			[](Engine::SceneSystem& scenes)
			{
				Game::Register(scenes);
			});
	}

	Game::SandboxHud* FindHud(Engine::Scene& scene)
	{
		for (const entt::entity entity : scene.GetEntitiesWithTag(Engine::Tags::Ui))
		{
			if (auto* hud = scene.GetBehavior<Game::SandboxHud>(entity))
			{
				return hud;
			}
		}

		return nullptr;
	}

} // namespace

SWIM_TEST("Game.Sandbox", "BuildsEveryPlaygroundHeadless")
{
	auto engine = MakeSandbox();
	SWIM_REQUIRE(engine.Started());
	SWIM_REQUIRE(engine.Tick(2));
	auto* sandbox = engine.SceneAs<Game::Sandbox>();
	SWIM_REQUIRE(sandbox != nullptr);
	SWIM_CHECK_EQUAL(engine->GetSceneSystem()->GetActiveSceneName(), std::string("Sandbox"));

	auto& registry = sandbox->GetRegistry();
	SWIM_CHECK_EQUAL(sandbox->CountWithTag(Game::GameTags::PbrGallery), std::size_t{ 28 });
	SWIM_CHECK_EQUAL(sandbox->CountWithTag(Game::GameTags::InstanceHall), std::size_t{ 24 * 24 });
	SWIM_CHECK(sandbox->CountWithTag(Game::GameTags::PhysicsToy) > 10);
	SWIM_CHECK(sandbox->CountWithTag(Game::GameTags::Glass) > 0);
	SWIM_CHECK(sandbox->CountWithTag(Game::GameTags::Emissive) > 0);
	SWIM_CHECK_EQUAL(sandbox->CountWithTag(Game::GameTags::Tentacle), std::size_t{ 6 });
	SWIM_CHECK(registry.view<Engine::Light>().size() >= 15);
	SWIM_CHECK_EQUAL(registry.view<Engine::ParticleEmitter>().size(), std::size_t{ 3 });
	SWIM_CHECK_EQUAL(registry.view<Engine::SkinnedMeshRenderer>().size(), std::size_t{ 6 });
	SWIM_CHECK(registry.view<Engine::UiCanvas>().size() >= 2);
	SWIM_CHECK(sandbox->GetCameraRig() != entt::null);
	SWIM_CHECK(sandbox->GetShooter() != nullptr);

	// Every shadowed light is one the scene meant to shadow.
	std::size_t shadowed = 0;

	for (const auto [entity, light] : registry.view<Engine::Light>().each())
	{
		shadowed += light.CastShadows ? 1u : 0u;
	}

	SWIM_CHECK_EQUAL(shadowed, std::size_t{ 3 });

	// The HUD and the world info panel have documents.
	auto* hud = FindHud(*sandbox);
	SWIM_REQUIRE(hud != nullptr);
	SWIM_CHECK(hud->GetDocument() != nullptr);
	SWIM_CHECK(sandbox->GetInfoDocument() != nullptr);
}

SWIM_TEST("Game.Sandbox", "BallsFallRestAndCountImpacts")
{
	auto engine = MakeSandbox();
	SWIM_REQUIRE(engine.Started());
	SWIM_REQUIRE(engine.Tick(1));
	auto* sandbox = engine.SceneAs<Game::Sandbox>();
	SWIM_REQUIRE(sandbox != nullptr);

	SWIM_CHECK(engine.Command("sandbox.drop 6"));
	SWIM_REQUIRE(engine.Tick(2));
	SWIM_CHECK_EQUAL(sandbox->CountWithTag(Game::GameTags::Spawned), std::size_t{ 6 });
	SWIM_CHECK_EQUAL(sandbox->CountWithTag(Engine::Tags::Projectile), std::size_t{ 6 });

	SWIM_REQUIRE(engine.Tick(150)); // 2.5 s.
	SWIM_CHECK(sandbox->GetImpacts() > 0);
	SWIM_CHECK(sandbox->GetStrongestImpact() > 0.0f);

	for (const entt::entity ball : sandbox->GetEntitiesWithTag(Game::GameTags::Spawned))
	{
		const float y = sandbox->GetRegistry().get<Engine::Transform>(ball).GetPosition().y;
		SWIM_CHECK(y > -1.0f); // Resting on something, not through the ground.
		SWIM_CHECK(y < 8.0f);
	}

	// Their Lifetime (10 s) removes them.
	SWIM_REQUIRE(engine.Tick(60 * 9));
	SWIM_CHECK_EQUAL(sandbox->CountWithTag(Game::GameTags::Spawned), std::size_t{ 0 });
}

SWIM_TEST("Game.Sandbox", "TheGroundColliderSpansTheWholeCheckeredPlane")
{
	// The visible plane is 130 m square; the collider used to stop at +-40 m, so balls
	// fell through the outer 25 m of every edge (the Sponza end included).
	auto engine = MakeSandbox();
	SWIM_REQUIRE(engine.Started());
	SWIM_REQUIRE(engine.Tick(1));
	auto* sandbox = engine.SceneAs<Game::Sandbox>();
	SWIM_REQUIRE(sandbox != nullptr);
	const std::vector<glm::vec3> spots{ { 62.0f, 2.0f, 62.0f }, { -62.0f, 2.0f, -62.0f }, { 50.0f, 2.0f, -3.0f }, { 5.0f, 2.0f, -60.0f } };
	std::vector<entt::entity> balls;

	for (const auto& spot : spots)
	{
		const entt::entity ball = sandbox->CreateEntity("Edge probe");
		sandbox->AddComponent<Engine::Transform>(ball, Engine::Transform(spot, glm::vec3(1.0f)));
		Game::AddSphereBody(*sandbox, ball, Engine::RigidbodyType::Dynamic, 0.5f, 1.0f);
		balls.push_back(ball);
	}

	SWIM_REQUIRE(engine.Tick(120)); // 2 s: long enough to fall through if nothing is there.

	for (const entt::entity ball : balls)
	{
		const float y = sandbox->GetRegistry().get<Engine::Transform>(ball).GetPosition().y;
		SWIM_CHECK(y > 0.3f && y < 0.7f); // Resting on the ground (radius 0.5).
	}
}

SWIM_TEST("Game.Sandbox", "PausedWorldHoldsStillAndStopRestoresIt")
{
	auto engine = MakeSandbox();
	SWIM_REQUIRE(engine.Started());
	SWIM_REQUIRE(engine.Tick(1));
	auto* sandbox = engine.SceneAs<Game::Sandbox>();
	SWIM_REQUIRE(sandbox != nullptr);
	const std::size_t baseline = sandbox->GetEntityCount();

	SWIM_CHECK(engine.Command("sandbox.drop 4"));
	SWIM_CHECK(engine.Command("sandbox.rain 1"));
	SWIM_REQUIRE(engine.Tick(30));
	SWIM_CHECK(sandbox->GetRainBalls());
	const std::size_t spawned = sandbox->CountWithTag(Game::GameTags::Spawned);
	SWIM_CHECK(spawned > 4);

	SWIM_CHECK(engine.Command("pause"));
	std::vector<glm::vec3> positions;

	for (const entt::entity ball : sandbox->GetEntitiesWithTag(Game::GameTags::Spawned))
	{
		positions.push_back(sandbox->GetRegistry().get<Engine::Transform>(ball).GetPosition());
	}

	SWIM_REQUIRE(engine.Tick(20));
	SWIM_CHECK_EQUAL(sandbox->CountWithTag(Game::GameTags::Spawned), spawned); // Rain is simulation time.
	std::size_t index = 0;

	for (const entt::entity ball : sandbox->GetEntitiesWithTag(Game::GameTags::Spawned))
	{
		const glm::vec3 now = sandbox->GetRegistry().get<Engine::Transform>(ball).GetPosition();
		SWIM_CHECK(index < positions.size() && glm::length(now - positions[index]) < 1e-5f);
		++index;
	}

	SWIM_CHECK(engine.Command("stop"));
	SWIM_CHECK(!sandbox->GetRainBalls());
	SWIM_REQUIRE(engine.Tick(1));
	SWIM_CHECK_EQUAL(sandbox->CountWithTag(Game::GameTags::Spawned), std::size_t{ 0 });
	SWIM_CHECK_EQUAL(sandbox->GetEntityCount(), baseline);
	SWIM_CHECK_EQUAL(sandbox->GetBuildCount(), 2u);
	SWIM_CHECK(FindHud(*sandbox) != nullptr);

	SWIM_CHECK(engine.Command("play"));
	SWIM_REQUIRE(engine.Tick(1));
	SWIM_CHECK(engine->GetEngineState() == EngineState::Playing);
}

SWIM_TEST("Game.Sandbox", "CommandsDriveTheSunTabsHudAndShooter")
{
	auto engine = MakeSandbox();
	SWIM_REQUIRE(engine.Started());
	SWIM_REQUIRE(engine.Tick(1));
	auto* sandbox = engine.SceneAs<Game::Sandbox>();
	SWIM_REQUIRE(sandbox != nullptr);

	SWIM_CHECK(engine.Command("sandbox.sun 60 120"));
	SWIM_CHECK_NEAR(sandbox->GetSunElevation(), 60.0f, 1e-4f);
	SWIM_CHECK_NEAR(sandbox->GetSunAzimuth(), 120.0f, 1e-4f);

	auto* hud = FindHud(*sandbox);
	SWIM_REQUIRE(hud != nullptr);
	SWIM_CHECK(engine.Command("sandbox.tab 2"));
	SWIM_REQUIRE(engine.Tick(1));
	SWIM_CHECK_EQUAL(hud->GetVisibleSection(), 2u);
	SWIM_CHECK(engine.Command("sandbox.tab 9")); // Clamped to the last tab (Scene).
	SWIM_REQUIRE(engine.Tick(1));
	SWIM_CHECK_EQUAL(hud->GetVisibleSection(), 3u);
	SWIM_CHECK(engine.Command("sandbox.hud 0"));
	SWIM_CHECK(!sandbox->IsHudVisible());
	SWIM_CHECK(engine.Command("sandbox.hud 1"));
	SWIM_CHECK(sandbox->IsHudVisible());

	const std::size_t before = sandbox->CountWithTag(Engine::Tags::Projectile);
	SWIM_CHECK(engine.Command("sandbox.fire"));
	SWIM_REQUIRE(engine.Tick(2));
	SWIM_CHECK_EQUAL(sandbox->CountWithTag(Engine::Tags::Projectile), before + 1);
}

SWIM_TEST("Game.Sandbox", "SingleStepsAdvanceThePlaygroundOneTickAtATime")
{
	auto engine = MakeSandbox();
	SWIM_REQUIRE(engine.Started());
	SWIM_REQUIRE(engine.Tick(1));
	auto* sandbox = engine.SceneAs<Game::Sandbox>();
	SWIM_REQUIRE(sandbox != nullptr);

	SWIM_CHECK(engine.Command("pause"));
	const std::uint64_t steps = sandbox->GetPhysicsStepCount();
	SWIM_REQUIRE(engine.Tick(5));
	SWIM_CHECK_EQUAL(sandbox->GetPhysicsStepCount(), steps);
	SWIM_CHECK(engine.Command("step 2"));
	SWIM_REQUIRE(engine.Tick(5));
	SWIM_CHECK_EQUAL(sandbox->GetPhysicsStepCount(), steps + 2);
	SWIM_CHECK(engine->GetEngineState() == EngineState::Paused);
}

SWIM_TEST("Game.Sandbox", "CameraBookmarksMoveTheMainCamera")
{
	auto engine = MakeSandbox();
	SWIM_REQUIRE(engine.Started());
	SWIM_REQUIRE(engine.Tick(1));
	auto* sandbox = engine.SceneAs<Game::Sandbox>();
	SWIM_REQUIRE(sandbox != nullptr);
	SWIM_CHECK_EQUAL(Game::Sandbox::GetBookmarkCount(), 10u);
	SWIM_CHECK_EQUAL(std::string(Game::Sandbox::GetBookmarkName(7)), std::string("Reflection lab"));
	SWIM_CHECK_EQUAL(std::string(Game::Sandbox::GetBookmarkName(3)), std::string("Physics"));

	SWIM_CHECK(engine.Command("sandbox.view 3"));
	const auto& camera = engine->GetCameraSystem()->GetCamera();
	SWIM_CHECK(glm::length(camera.GetPosition() - glm::vec3(7.0f, 5.0f, 19.0f)) < 1e-4f);
	const glm::vec3 toTarget = glm::normalize(glm::vec3(0.0f, 1.2f, 9.0f) - camera.GetPosition());
	SWIM_CHECK(glm::dot(camera.GetForward(), toTarget) > 0.999f);
	SWIM_CHECK(!sandbox->GoToBookmark(99));

	// Every view works, including ones with labels behind the camera.
	for (std::uint32_t view = 0; view < Game::Sandbox::GetBookmarkCount(); ++view)
	{
		SWIM_CHECK(engine.Command("sandbox.view " + std::to_string(view)));
		SWIM_CHECK(engine.Tick(2));
	}

	// The camera flies (and bookmarks work) while paused.
	SWIM_CHECK(engine.Command("pause"));
	SWIM_CHECK(sandbox->GoToBookmark(0));
	SWIM_REQUIRE(engine.Tick(1));
	SWIM_CHECK(glm::length(camera.GetPosition() - glm::vec3(2.0f, 7.5f, 26.0f)) < 1e-4f);
}

SWIM_TEST("Game.Sandbox", "StartupCommandsRunBeforeTheSceneAndStillApply")
{
	// --exec commands run at the end of engine Init, before the scene's first Init.
	Swim::Tests::HeadlessEngine engine(
		[](Engine::SceneSystem& scenes)
		{
			Game::Register(scenes);
		},
		"", EngineState::Playing, { "sandbox.view 2", "sandbox.sun 55 90", "sandbox.tab 2", "timescale 0.5" });
	SWIM_REQUIRE(engine.Started());
	// The HUD's first frame must not report its controls' initial values as changes
	// (the bookmark dropdown would otherwise jump back to the overview).
	SWIM_REQUIRE(engine.Tick(2));
	const auto& camera = engine->GetCameraSystem()->GetCamera();
	auto* sandbox = engine.SceneAs<Game::Sandbox>();
	SWIM_REQUIRE(sandbox != nullptr);
	SWIM_CHECK(glm::length(camera.GetPosition() - glm::vec3(12.0f, 9.0f, 9.0f)) < 1e-4f);
	SWIM_CHECK_NEAR(sandbox->GetSunElevation(), 55.0f, 1e-4f);
	SWIM_CHECK_NEAR(engine->GetClock().GetTimeScale(), 0.5, 1e-9); // Not reset by the time-scale slider.
	auto* hud = FindHud(*sandbox);
	SWIM_REQUIRE(hud != nullptr);
	SWIM_CHECK_EQUAL(hud->GetVisibleSection(), 2u);
	SWIM_CHECK_NEAR(hud->GetDocument()->GetValue(hud->GetBookmarkDropdown()), 2.0f, 1e-6f); // Shows the current view.
}

SWIM_TEST("Game.Sandbox", "TheFourTabsFitInsideThePanel")
{
	auto engine = MakeSandbox();
	SWIM_REQUIRE(engine.Started());
	SWIM_REQUIRE(engine.Tick(1));
	auto* sandbox = engine.SceneAs<Game::Sandbox>();
	SWIM_REQUIRE(sandbox != nullptr);
	auto* hud = FindHud(*sandbox);
	SWIM_REQUIRE(hud != nullptr);
	auto& document = *hud->GetDocument();

	for (const auto size : { Swim::UI::UiPoint{ 1280.0f, 720.0f }, Swim::UI::UiPoint{ 1920.0f, 1080.0f } })
	{
		document.Layout(size);
		const auto panel = document.GetBounds(hud->GetPanel());
		SWIM_CHECK(panel.Width > 0.0f);
		float previousRight = panel.X;

		for (const auto tab : hud->GetTabOptions())
		{
			const auto bounds = document.GetBounds(tab);
			SWIM_CHECK(bounds.Width > 0.0f);
			SWIM_CHECK(bounds.X >= previousRight - 0.5f); // One row, left to right, no overlap.
			SWIM_CHECK(bounds.X + bounds.Width <= panel.X + panel.Width - 4.0f);
			SWIM_CHECK(bounds.Y >= panel.Y && bounds.Y + bounds.Height <= panel.Y + panel.Height);
			previousRight = bounds.X + bounds.Width;
		}
	}

	// Each tab shows its own section.
	for (std::uint32_t tab = 0; tab < Game::Sandbox::SandboxTabCount; ++tab)
	{
		SWIM_CHECK(engine.Command("sandbox.tab " + std::to_string(tab)));
		SWIM_REQUIRE(engine.Tick(1));
		SWIM_CHECK_EQUAL(hud->GetVisibleSection(), tab);
	}
}

SWIM_TEST("Game.Sandbox", "CameraPresetsDriveTheFeaturesAndTheControlsFollow")
{
	auto engine = MakeSandbox();
	SWIM_REQUIRE(engine.Started());
	SWIM_REQUIRE(engine.Tick(1));
	auto* sandbox = engine.SceneAs<Game::Sandbox>();
	SWIM_REQUIRE(sandbox != nullptr);
	SWIM_REQUIRE(sandbox->GetDepthOfField() && sandbox->GetCameraLens() && sandbox->GetFilmSensor());
	// Off by default: the frame is untouched.
	SWIM_CHECK(!sandbox->GetDepthOfField()->Enabled && !sandbox->GetCameraLens()->Enabled && !sandbox->GetFilmSensor()->Enabled);

	// Headless runs have no render settings; the grading checks need them.
	auto* services = sandbox->GetRenderServices();
	Engine::RenderSettings* settings = services ? services->Settings : nullptr;
	SWIM_CHECK(engine.Command("sandbox.camera 2")); // Cinematic 35mm.
	SWIM_CHECK(sandbox->GetCameraPreset() == Engine::CameraPreset::Cinematic35mm);
	SWIM_CHECK(sandbox->GetDepthOfField()->Enabled && sandbox->GetCameraLens()->Enabled && sandbox->GetFilmSensor()->Enabled);
	SWIM_CHECK(sandbox->GetCameraLens()->Settings.Halation > 0.0f);

	if (settings)
	{
		SWIM_CHECK(settings->Post.Grading.Temperature > 0.0f); // Warmer than neutral.
		Swim::Render::ValidatePostProcessSettings(settings->Post);
	}

	// A console edit after the preset shows up in the panel (the controls are bound both ways).
	sandbox->GetCameraLens()->Settings.Vignette = 1.25f;
	SWIM_REQUIRE(engine.Tick(2));
	auto* hud = FindHud(*sandbox);
	SWIM_REQUIRE(hud != nullptr);
	SWIM_CHECK_NEAR(sandbox->GetCameraLens()->Settings.Vignette, 1.25f, 1e-6f); // The sync did not write it back.

	SWIM_CHECK(engine.Command("sandbox.camera 0"));
	SWIM_CHECK(!sandbox->GetDepthOfField()->Enabled && !sandbox->GetCameraLens()->Enabled && !sandbox->GetFilmSensor()->Enabled);

	if (settings)
	{
		SWIM_CHECK_NEAR(settings->Post.Grading.Temperature, 0.0f, 1e-6f);
		SWIM_CHECK_NEAR(settings->Post.Grading.Saturation, Game::Sandbox::TropicalSaturation, 1e-6f);
	}

	SWIM_CHECK(engine.Command("sandbox.dof 1.4 3"));
	SWIM_CHECK(sandbox->GetDepthOfField()->Enabled);
	SWIM_CHECK_NEAR(sandbox->GetDepthOfField()->Settings.FocusDistance, 3.0f, 1e-6f);
	SWIM_REQUIRE(engine.Tick(2)); // Rendering headless with the camera features on.
}

SWIM_TEST("Game.Sandbox", "TheBlackHoleLensesAndDragsOnACameraFacingPlane")
{
	auto engine = MakeSandbox();
	SWIM_REQUIRE(engine.Started());
	SWIM_REQUIRE(engine.Tick(1));
	auto* sandbox = engine.SceneAs<Game::Sandbox>();
	SWIM_REQUIRE(sandbox != nullptr);
	const entt::entity hole = sandbox->GetBlackHole();
	SWIM_REQUIRE(hole != entt::null);
	auto& registry = sandbox->GetRegistry();
	SWIM_CHECK(registry.all_of<Engine::Pickable>(hole));
	auto* behaviour = sandbox->GetBehavior<Game::BlackHole>(hole);
	auto* drag = sandbox->GetBehavior<Game::MouseDrag>(hole);
	SWIM_REQUIRE(behaviour != nullptr && drag != nullptr);
	SWIM_CHECK_EQUAL(sandbox->CountWithTag(Game::GameTags::BlackHole), std::size_t(1)); // Nothing rasterized: the gas is traced.

	// The lens follows the entity.
	auto* lensing = sandbox->GetLensing();
	SWIM_REQUIRE(lensing != nullptr);
	SWIM_REQUIRE_EQUAL(lensing->Lenses.size(), std::size_t(1));
	const glm::vec3 home = Game::Sandbox::GetBlackHoleHome();
	SWIM_CHECK_NEAR(lensing->Lenses[0].Position[0], home.x, 1e-4f);
	SWIM_CHECK_NEAR(lensing->Lenses[0].SchwarzschildRadius, behaviour->GetSchwarzschildRadius(), 1e-6f);

	// Look at it, grab it where it is on screen, and move the mouse 120 px right.
	SWIM_CHECK(engine.Command("sandbox.view 9"));
	SWIM_REQUIRE(engine.Tick(1));
	auto* cameras = engine->GetCameraSystem();
	const auto& camera = cameras->GetCamera();
	const auto screen = cameras->WorldToScreen(home);
	SWIM_REQUIRE(screen.has_value());
	drag->Press(10.0f, 10.0f); // The sky: nothing to grab.
	SWIM_CHECK(!drag->IsDragging());
	drag->Press((*screen).x, (*screen).y);
	SWIM_REQUIRE(drag->IsDragging());
	drag->Move((*screen).x + 120.0f, (*screen).y);
	const glm::vec3 moved = registry.get<Engine::Transform>(hole).GetWorldPosition(registry);
	const glm::vec3 forward = camera.GetForward();
	SWIM_CHECK_NEAR(glm::dot(moved - camera.GetPosition(), forward), glm::dot(home - camera.GetPosition(), forward), 1e-3f);
	const auto after = cameras->WorldToScreen(moved);
	SWIM_REQUIRE(after.has_value());
	SWIM_CHECK_NEAR((*after).x, (*screen).x + 120.0f, 0.5f);
	SWIM_CHECK_NEAR((*after).y, (*screen).y, 0.5f);
	drag->Release();
	SWIM_CHECK(!drag->IsDragging());
	SWIM_REQUIRE(engine.Tick(1));
	SWIM_CHECK_NEAR(lensing->Lenses[0].Position[0], moved.x, 1e-4f);

	// The ball shooter is untouched.
	const std::size_t before = sandbox->CountWithTag(Engine::Tags::Projectile);
	SWIM_CHECK(engine.Command("sandbox.fire"));
	SWIM_REQUIRE(engine.Tick(2));
	SWIM_CHECK_EQUAL(sandbox->CountWithTag(Engine::Tags::Projectile), before + 1);
}

SWIM_TEST("Game.Sandbox", "SceneProfilingSwitchesHideTheirParts")
{
	auto engine = MakeSandbox();
	SWIM_REQUIRE(engine.Started());
	SWIM_REQUIRE(engine.Tick(1));
	auto* sandbox = engine.SceneAs<Game::Sandbox>();
	SWIM_REQUIRE(sandbox != nullptr);
	auto& registry = sandbox->GetRegistry();
	const auto enabledLights = [&]
	{
		std::size_t count = 0;

		for (const auto [entity, light] : registry.view<Engine::Light>().each())
		{
			(void)entity;
			count += light.Enabled ? 1u : 0u;
		}

		return count;
	};
	const std::size_t before = enabledLights();
	SWIM_CHECK(engine.Command("render.toggle scene.light-swarm 0"));
	SWIM_CHECK(!sandbox->IsGroupShown(Game::GameTags::SwarmLight));
	SWIM_CHECK_EQUAL(enabledLights(), before - sandbox->CountWithTag(Game::GameTags::SwarmLight));
	SWIM_CHECK(engine.Command("render.toggle scene.* 1"));
	SWIM_CHECK_EQUAL(enabledLights(), before);
	SWIM_CHECK(engine.Command("render.toggles"));
	bool rejected = false;

	try
	{
		rejected = !engine.Command("render.toggle no.such.switch 0");
	}
	catch (const std::exception&)
	{
		rejected = true;
	}

	SWIM_CHECK(rejected);
	SWIM_CHECK(engine.Command("profile 3 1"));
	SWIM_REQUIRE(engine.Tick(6));

	// bench: a baseline and one capture per switch turned off alone, restored after.
	const auto csv = std::filesystem::temp_directory_path() /
		("swim-bench-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".csv");
	{
		std::error_code ignored;
		std::filesystem::remove(csv, ignored);
	}
	SWIM_CHECK(engine.Command("bench " + csv.string() + " 2 0 ablate hall=sandbox.view 2"));
	const std::size_t switches = engine->GetRenderServices().Toggles->List().size();
	SWIM_REQUIRE(engine.Tick(static_cast<std::uint32_t>(4 * (switches + 2))));
	std::size_t baselineRows = 0;
	std::size_t swarmRows = 0;
	{
		std::ifstream in(csv);
		std::string line;

		while (std::getline(in, line))
		{
			baselineRows += line.starts_with("hall|baseline,") ? 1u : 0u;
			swarmRows += line.starts_with("hall|-scene.light-swarm,") ? 1u : 0u;
		}
	}
	SWIM_CHECK(baselineRows > 0);
	SWIM_CHECK(swarmRows > 0);
	SWIM_CHECK(sandbox->IsGroupShown(Game::GameTags::SwarmLight)); // Restored.
	{
		std::error_code ignored;
		std::filesystem::remove(csv, ignored);
	}
}

SWIM_TEST("Game.LightSwarm", "MembersSteerTowardTargetsAndStayInTheBox")
{
	Game::LightSwarm::Settings settings;
	settings.BoxMin = { -2.0f, 0.0f, -1.0f };
	settings.BoxMax = { 2.0f, 3.0f, 1.0f };
	glm::vec3 position(-1.5f, 0.5f, 0.0f);
	glm::vec3 velocity(0.0f);
	const glm::vec3 target(1.5f, 2.5f, 0.5f);
	const float start = glm::length(target - position);

	for (int i = 0; i < 120; ++i)
	{
		position = Game::LightSwarm::Step(settings, position, velocity, target, 2.0f, 1.0f / 60.0f);
		SWIM_CHECK(glm::all(glm::greaterThanEqual(position, settings.BoxMin)) && glm::all(glm::lessThanEqual(position, settings.BoxMax)));
	}

	SWIM_CHECK(glm::length(target - position) < start * 0.5f);
	SWIM_CHECK(glm::length(velocity) <= 2.0f + 1e-3f);

	// A member pushed outside bounces back in.
	glm::vec3 outward(0.0f, 0.0f, 50.0f);
	const glm::vec3 clamped = Game::LightSwarm::Step(settings, { 0.0f, 1.0f, 0.9f }, outward, { 0.0f, 1.0f, 5.0f }, 2.0f, 0.1f);
	SWIM_CHECK_NEAR(clamped.z, 1.0f, 1e-5f);
	SWIM_CHECK(outward.z < 0.0f);
}

SWIM_TEST("Game.Sandbox", "TheLightSwarmRoamsItsBoxWhilePlayingAndFreezesWhenPaused")
{
	auto engine = MakeSandbox();
	SWIM_REQUIRE(engine.Started());
	SWIM_REQUIRE(engine.Tick(1));
	auto* sandbox = engine.SceneAs<Game::Sandbox>();
	SWIM_REQUIRE(sandbox != nullptr);
	SWIM_CHECK_EQUAL(sandbox->CountWithTag(Game::GameTags::SwarmLight), std::size_t{ 256 });
	auto* swarm = sandbox->GetBehavior<Game::LightSwarm>(sandbox->GetSwarmController());
	SWIM_REQUIRE(swarm != nullptr);
	SWIM_CHECK_EQUAL(swarm->GetCount(), std::size_t{ 256 });
	SWIM_CHECK(!sandbox->IsSponzaLoaded()); // No renderer, no model: the default atrium box.

	auto& registry = sandbox->GetRegistry();
	const auto positions = [&]
	{
		std::vector<glm::vec3> result;

		for (const entt::entity light : sandbox->GetEntitiesWithTag(Game::GameTags::SwarmLight))
		{
			SWIM_CHECK(registry.all_of<Engine::Light>(light));
			result.push_back(registry.get<Engine::Transform>(light).GetPosition());
		}

		return result;
	};
	const auto before = positions();
	SWIM_REQUIRE(engine.Tick(60));
	const auto after = positions();
	SWIM_REQUIRE_EQUAL(before.size(), after.size());
	std::size_t moved = 0;

	for (std::size_t i = 0; i < after.size(); ++i)
	{
		moved += glm::length(after[i] - before[i]) > 0.05f ? 1u : 0u;
		SWIM_CHECK(glm::all(glm::greaterThanEqual(after[i], sandbox->GetSwarmMin() - glm::vec3(1e-4f))));
		SWIM_CHECK(glm::all(glm::lessThanEqual(after[i], sandbox->GetSwarmMax() + glm::vec3(1e-4f))));
	}

	SWIM_CHECK(moved > after.size() * 9 / 10);

	SWIM_CHECK(engine.Command("pause"));
	const auto paused = positions();
	SWIM_REQUIRE(engine.Tick(20));
	const auto stillPaused = positions();

	for (std::size_t i = 0; i < paused.size(); ++i)
	{
		SWIM_CHECK(glm::length(stillPaused[i] - paused[i]) < 1e-6f);
	}

	// Stop rebuilds the swarm (same count, fresh controller).
	SWIM_CHECK(engine.Command("stop"));
	SWIM_REQUIRE(engine.Tick(1));
	SWIM_CHECK_EQUAL(sandbox->CountWithTag(Game::GameTags::SwarmLight), std::size_t{ 256 });
	SWIM_CHECK(sandbox->GetBehavior<Game::LightSwarm>(sandbox->GetSwarmController()) != nullptr);
}

SWIM_TEST("Game.Sandbox", "F1SwitchHidesEveryUiCanvas")
{
	auto engine = MakeSandbox();
	SWIM_REQUIRE(engine.Started());
	SWIM_REQUIRE(engine.Tick(1));
	auto* sandbox = engine.SceneAs<Game::Sandbox>();
	SWIM_REQUIRE(sandbox != nullptr);
	const auto countVisible = [&]
	{
		std::size_t visible = 0;

		for (auto [entity, canvas] : sandbox->GetRegistry().view<Engine::UiCanvas>().each())
		{
			(void)entity;
			visible += canvas.Visible ? 1u : 0u;
		}

		return visible;
	};
	const std::size_t total = sandbox->GetRegistry().view<Engine::UiCanvas>().size();
	SWIM_CHECK(total >= 2);
	SWIM_CHECK_EQUAL(countVisible(), total);
	SWIM_CHECK(engine.Command("sandbox.hud 0")); // What F1 does.
	SWIM_REQUIRE(engine.Tick(2));				 // The HUD applies it; the UI runtime drops the canvases next frame.
	SWIM_CHECK_EQUAL(countVisible(), std::size_t{ 0 });
	SWIM_CHECK_EQUAL(engine->GetUiRuntime()->GetCanvasCount(), 0u);
	SWIM_CHECK(engine.Command("sandbox.hud 1"));
	SWIM_REQUIRE(engine.Tick(1));
	SWIM_CHECK_EQUAL(countVisible(), total);
}

SWIM_TEST("Game.Sandbox", "TheReflectionLabHasProbesAndADynamicFloor")
{
	auto engine = MakeSandbox();
	SWIM_REQUIRE(engine.Started());
	SWIM_REQUIRE(engine.Tick(1));
	auto* sandbox = engine.SceneAs<Game::Sandbox>();
	SWIM_REQUIRE(sandbox != nullptr);
	auto& registry = sandbox->GetRegistry();

	// Every regression case is there: the touching pair, the ball on the floor, the mirror
	// cube, two more polished chrome balls, the orbiting chrome ball, and one area probe.
	const auto lab = Game::Sandbox::GetReflectionLabCenter();
	std::size_t objectProbes = 0, areaProbes = 0, labProbes = 0;

	for (auto [entity, probe, transform] : registry.view<Engine::ReflectionProbe, Engine::Transform>().each())
	{
		objectProbes += probe.ObjectProbe ? 1u : 0u;
		areaProbes += probe.ObjectProbe ? 0u : 1u;
		labProbes += glm::length(transform.GetWorldPosition(registry) - lab) < 8.0f ? 1u : 0u;
	}

	SWIM_CHECK_EQUAL(labProbes, std::size_t{ 8 });		  // 6 chrome spheres, the mirror cube, the area probe.
	SWIM_CHECK_EQUAL(objectProbes, std::size_t{ 7 + 6 }); // + the playground's six chrome balls.
	SWIM_CHECK_EQUAL(areaProbes, std::size_t{ 2 });		  // The lab and the PBR gallery.
	SWIM_CHECK(objectProbes + areaProbes <= 16u);		  // The sandbox's MaxProbes.
	// Planar reflectors: the mirror cube and every chrome ball (their camera-facing caps).
	std::size_t planars = 0, labPlanars = 0;

	for (auto [entity, reflector, transform] : registry.view<Engine::PlanarReflector, Engine::Transform>().each())
	{
		++planars;
		labPlanars += glm::length(transform.GetWorldPosition(registry) - lab) < 8.0f ? 1u : 0u;
	}

	SWIM_CHECK_EQUAL(labPlanars, std::size_t{ 7 });
	SWIM_CHECK_EQUAL(planars, std::size_t{ 7 + 6 });
	// The two spheres of the pair are 1 cm apart.
	glm::vec3 a{ 0.0f }, b{ 0.0f };

	for (auto [entity, transform] : registry.view<Engine::Transform>().each())
	{
		const auto name = sandbox->GetEntityName(entity);
		a = name == "Lab chrome pair A" ? transform.GetWorldPosition(registry) : a;
		b = name == "Lab chrome pair B" ? transform.GetWorldPosition(registry) : b;
	}

	SWIM_CHECK(std::abs(glm::length(a - b) - 1.01f) < 1.0e-4f);

	// The floor: checker (white), green, an animated rainbow, then removed (hidden), from the
	// command line; the rainbow keeps changing even while paused (a real-time behaviour).
	auto* floor = sandbox->GetLabFloor();
	SWIM_REQUIRE(floor != nullptr);
	SWIM_CHECK(floor->GetMode() == Game::ReflectionLabFloor::Mode::Checker);
	SWIM_CHECK(engine.Command("sandbox.labfloor 1"));
	SWIM_CHECK(floor->GetMode() == Game::ReflectionLabFloor::Mode::Green);
	SWIM_CHECK(floor->GetTint().g > 3.0f * floor->GetTint().r);
	SWIM_CHECK(engine.Command("sandbox.labfloor 2"));
	SWIM_CHECK(engine.Command("pause"));
	const glm::vec3 before = floor->GetTint();
	SWIM_REQUIRE(engine.Tick(2));
	floor->Update(1.0); // One second of real time (headless ticks take microseconds).
	const glm::vec3 after = floor->GetTint();
	SWIM_CHECK(glm::length(after - before) > 0.05f);
	// A full hue turn visits red, green and blue dominance.
	int dominant[3] = { 0, 0, 0 };

	for (int i = 0; i < 12; ++i)
	{
		const auto tint = Game::ReflectionLabFloor::TintFor(Game::ReflectionLabFloor::Mode::Rainbow, float(i) * 0.5f);
		const int c = tint.r >= tint.g && tint.r >= tint.b ? 0 : (tint.g >= tint.b ? 1 : 2);
		++dominant[c];
	}

	SWIM_CHECK(dominant[0] > 0 && dominant[1] > 0 && dominant[2] > 0);
	SWIM_CHECK(engine.Command("sandbox.labfloor 3"));
	entt::entity pad = entt::null;

	for (auto [entity, renderer] : registry.view<Engine::MeshRenderer>().each())
	{
		pad = sandbox->GetEntityName(entity) == "Reflection lab floor" ? entity : pad;
	}

	SWIM_REQUIRE(pad != entt::null);
	const auto flags = static_cast<std::uint32_t>(registry.get<Engine::MeshRenderer>(pad).Flags);
	SWIM_CHECK((flags & static_cast<std::uint32_t>(Swim::Render::RenderObjectFlags::Visible)) == 0u);
	SWIM_CHECK(engine.Command("sandbox.labfloor 0"));
	SWIM_CHECK((static_cast<std::uint32_t>(registry.get<Engine::MeshRenderer>(pad).Flags) &
				   static_cast<std::uint32_t>(Swim::Render::RenderObjectFlags::Visible)) != 0u);
}
