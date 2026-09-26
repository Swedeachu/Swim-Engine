#include "Engine/Components/CameraComponent.h"
#include "Engine/Components/Light.h"
#include "Engine/Components/ParticleEmitter.h"
#include "Engine/Components/SkinnedMeshRenderer.h"
#include "Engine/Components/Transform.h"
#include "Engine/Components/UiCanvas.h"
#include "Engine/Systems/Camera/CameraSystem.h"
#include "Engine/Systems/Scene/SceneSystem.h"
#include "Game/Behaviors/BallShooter.h"
#include "Game/Behaviors/LightSwarm.h"
#include "Game/ModelImport.h"
#include "Engine/Assets/AssetSystem.h"
#include "Engine/Components/Light.h"
#include "Game/Findings.h"
#include "Game/Game.h"
#include "Game/SandboxContent.h"
#include "Game/Scenes/Sandbox.h"
#include "Game/Ui/SandboxHud.h"
#include "Game/Ui/UiBindings.h"
#include "Engine/Runtime/UiRuntime.h"
#include "Engine/Systems/UI/UiWidgets.h"
#include "Tests/Framework/Test.h"
#include "Tests/Suites/Engine/HeadlessEngine.h"

#include <filesystem>
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
	SWIM_CHECK_EQUAL(hud->GetVisibleSection(), 2u);
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
	SWIM_CHECK_EQUAL(Game::Sandbox::GetBookmarkCount(), 7u);
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

SWIM_TEST("Game.UiBindings", "HandlersSeeChangesButNotTheInitialState")
{
	Engine::UiRuntime ui{ std::filesystem::path(SWIM_RESOURCE_DIR) };
	auto document = ui.CreateDocument();
	Swim::UI::UiSliderDesc desc;
	desc.Min = 0.0f;
	desc.Max = 10.0f;
	desc.Value = 3.0f;
	const auto slider = Swim::UI::CreateSlider(*document, document->GetRoot(), desc);
	const auto box = Swim::UI::CreateCheckbox(*document, document->GetRoot(), "Box", Swim::UI::UiCheckState::Checked);

	Game::UiBindings bindings;
	std::vector<float> values;
	std::vector<bool> checks;
	bindings.OnValue(slider,
		[&](float value)
		{
			values.push_back(value);
		});
	bindings.OnChecked(box,
		[&](bool checked)
		{
			checks.push_back(checked);
		});
	bindings.Process(*document);
	SWIM_CHECK(values.empty());
	SWIM_CHECK(checks.empty());

	document->SetValue(slider, 7.0f);
	document->SetChecked(box, Swim::UI::UiCheckState::Unchecked);
	bindings.Process(*document);
	bindings.Process(*document); // No change: no second call.
	SWIM_REQUIRE_EQUAL(values.size(), std::size_t{ 1 });
	SWIM_CHECK_NEAR(values[0], 7.0f, 1e-6f);
	SWIM_REQUIRE_EQUAL(checks.size(), std::size_t{ 1 });
	SWIM_CHECK(!checks[0]);
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

SWIM_TEST("Game.ModelImport", "FindSponzaPrefersTheDracoKtxGlb")
{
	Swim::Assets::AssetSystem assets;
	SWIM_REQUIRE(assets.Initialize());
	for (const char* path : { "Models/Sponza/sponza-ktx.model", "Models/Sponza/sponza-ktx-draco.model", "Models/Sponza/glTF/Sponza.model",
			 "Models/Barrel/barrel.model" })
	{
		const auto handle = assets.Declare<Swim::Assets::ModelAsset>(path);
		SWIM_REQUIRE(assets.Publish(handle, Swim::Assets::ModelAsset{}));
	}
	const auto path = [&](auto handle)
	{
		return assets.GetDatabase().FindPath(handle.GetId()).value_or(std::string());
	};
	const auto sponza = Game::FindSponzaModel(assets);
	SWIM_REQUIRE(sponza.IsValid());
	SWIM_CHECK_EQUAL(path(sponza), std::string("Models/Sponza/sponza-ktx-draco.model"));
	// Keyword filtering, preference order and avoidance.
	const auto plain = Game::FindCookedModel(assets, { "sponza" }, { "gltf/sponza" }, { "ktx" });
	SWIM_REQUIRE(plain.IsValid());
	SWIM_CHECK_EQUAL(path(plain), std::string("Models/Sponza/glTF/Sponza.model"));
	const auto ktxOnly = Game::FindCookedModel(assets, { "sponza", "ktx" }, {}, { "draco" });
	SWIM_REQUIRE(ktxOnly.IsValid());
	SWIM_CHECK_EQUAL(path(ktxOnly), std::string("Models/Sponza/sponza-ktx.model"));
	SWIM_CHECK(!Game::FindCookedModel(assets, { "helmet" }).IsValid());
	assets.Shutdown();
}

SWIM_TEST("Game.Findings", "EveryFindingIsDocumentedWithAKnownStatus")
{
	const auto findings = Game::GetFindings();
	SWIM_CHECK(findings.size() >= 10);
	std::set<std::string_view> titles;
	for (const auto& finding : findings)
	{
		SWIM_CHECK(!finding.Title.empty());
		SWIM_CHECK(!finding.Detail.empty());
		SWIM_CHECK(finding.Status == "Fixed" || finding.Status == "Workaround" || finding.Status == "Open");
		SWIM_CHECK(titles.insert(finding.Title).second);
	}
}
