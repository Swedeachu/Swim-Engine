#include "Engine/Components/Tags.h"
#include "Engine/Components/Transform.h"
#include "Engine/Runtime/EngineStateMachine.h"
#include "Engine/Systems/Entity/Behavior.h"
#include "Engine/Systems/Scene/Scene.h"
#include "Engine/Systems/Scene/SceneCommandBuffer.h"
#include "Engine/Systems/Scene/SceneSystem.h"
#include "Game/SandboxContent.h"
#include "Tests/Framework/Test.h"
#include "Tests/Suites/Engine/HeadlessEngine.h"

#include <memory>
#include <string>
#include <vector>

using Engine::EngineState;

namespace
{
	// What the probes saw (shared by the scene factory and the tests).
	struct ProbeLog
	{
		std::vector<std::string> Events;
		int SceneInits = 0;
		int SceneExits = 0;
		int SceneAwakes = 0;
		int PlayingUpdates = 0;
		int PlayingFixed = 0;
		int AlwaysUpdates = 0;
		int RealTimeUpdates = 0;
		double LastPlayingDelta = -1.0;
		double LastAlwaysDelta = -1.0;
		double LastRealTimeDelta = -1.0;
		int CollisionsEntered = 0;
		std::vector<std::pair<EngineState, EngineState>> SceneTransitions;

		int Count(const std::string& event) const
		{
			int count = 0;
			for (const auto& value : Events)
			{
				count += value == event ? 1 : 0;
			}
			return count;
		}
	};

	enum class ProbeKind
	{
		Playing,  // Default mask: Playing only.
		Always,	  // Playing | Paused | Stopped, simulation time.
		RealTime, // Playing | Paused | Stopped, wall-clock time.
	};

	class Probe : public Engine::Behavior
	{
	  public:
		Probe(Engine::Scene* scene, entt::entity owner, ProbeLog* logValue, ProbeKind kindValue)
			: Behavior(scene, owner), log(logValue), kind(kindValue)
		{
		}

		int Awake() override
		{
			log->Events.push_back(Name("awake"));
			return 0;
		}

		int Init() override
		{
			log->Events.push_back(Name("init"));
			return 0;
		}

		void Update(double dt) override
		{
			switch (kind)
			{
			case ProbeKind::Playing:
				++log->PlayingUpdates;
				log->LastPlayingDelta = dt;
				break;
			case ProbeKind::Always:
				++log->AlwaysUpdates;
				log->LastAlwaysDelta = dt;
				break;
			case ProbeKind::RealTime:
				++log->RealTimeUpdates;
				log->LastRealTimeDelta = dt;
				break;
			}
		}

		void FixedUpdate(unsigned int) override
		{
			if (kind == ProbeKind::Playing)
			{
				++log->PlayingFixed;
			}
		}

		int Exit() override
		{
			log->Events.push_back(Name("exit"));
			return 0;
		}

		bool UsesRealTime() const override { return kind == ProbeKind::RealTime; }

		void OnPlay() override { log->Events.push_back(Name("play")); }

		void OnPause() override { log->Events.push_back(Name("pause")); }

		void OnResume() override { log->Events.push_back(Name("resume")); }

		void OnStop() override { log->Events.push_back(Name("stop")); }

		void OnCollisionEnter(const Engine::BehaviorCollision&) override { ++log->CollisionsEntered; }

	  private:
		std::string Name(const char* event) const
		{
			const char* prefix = kind == ProbeKind::Playing ? "playing." : (kind == ProbeKind::Always ? "always." : "realtime.");
			return std::string(prefix) + event;
		}

		ProbeLog* log;
		ProbeKind kind;
	};

	constexpr Engine::TagId CrateTag = Engine::MakeTag("Test.Crate");

	class ProbeScene : public Engine::Scene
	{
	  public:
		ProbeScene(const std::string& name, ProbeLog* logValue) : Scene(name), log(logValue) {}

		int Awake() override
		{
			++log->SceneAwakes;
			return 0;
		}

		int Init() override
		{
			++log->SceneInits;
			const EngineState all = EngineState::Playing | EngineState::Paused | EngineState::Stopped;

			const entt::entity playing = CreateEntity("Playing probe");
			AddComponent<Engine::Transform>(playing, Engine::Transform({ 0, 0, 0 }, glm::vec3(1.0f)));
			EmplaceBehavior<Probe>(playing, log, ProbeKind::Playing);

			const entt::entity always = CreateEntity("Always probe");
			AddComponent<Engine::Transform>(always, Engine::Transform({ 0, 0, 0 }, glm::vec3(1.0f)));
			EmplaceBehavior<Probe>(always, log, ProbeKind::Always);
			SetEnabledStates(always, all);

			const entt::entity realTime = CreateEntity("Real-time probe");
			AddComponent<Engine::Transform>(realTime, Engine::Transform({ 0, 0, 0 }, glm::vec3(1.0f)));
			EmplaceBehavior<Probe>(realTime, log, ProbeKind::RealTime);
			SetEnabledStates(realTime, all);

			for (int i = 0; i < 3; ++i)
			{
				const entt::entity crate = CreateEntity("Crate " + std::to_string(i));
				AddComponent<Engine::Transform>(crate, Engine::Transform({ static_cast<float>(i), 0, 0 }, glm::vec3(1.0f)));
				AddTag(crate, "Test.Crate");
				AddTag(crate, Engine::Tags::Static);
			}
			return 0;
		}

		int Exit() override
		{
			++log->SceneExits;
			return 0;
		}

		void OnStateChanged(EngineState previous, EngineState current) override { log->SceneTransitions.emplace_back(previous, current); }

	  private:
		ProbeLog* log;
	};

	// A second scene for switching.
	class EmptyScene : public Engine::Scene
	{
	  public:
		using Scene::Scene;
	};

	void RegisterProbeScenes(Engine::SceneSystem& scenes, ProbeLog& log)
	{
		scenes.RegisterSceneType("Probe",
			[&log](const std::string& name)
			{
				return std::static_pointer_cast<Engine::Scene>(std::make_shared<ProbeScene>(name, &log));
			});
		scenes.RegisterSceneType<EmptyScene>("Empty");
		scenes.SetStartupScene("Probe");
	}
} // namespace

SWIM_TEST("Engine.SceneRuntime", "BehavioursRunTheirLifecycleOnceInOrder")
{
	ProbeLog log;
	{
		Swim::Tests::HeadlessEngine engine(
			[&log](Engine::SceneSystem& scenes)
			{
				RegisterProbeScenes(scenes, log);
			});
		SWIM_REQUIRE(engine.Started());
		SWIM_CHECK_EQUAL(engine->GetSceneSystem()->GetActiveSceneName(), std::string("Probe"));
		SWIM_CHECK_EQUAL(log.SceneAwakes, 1);
		SWIM_CHECK_EQUAL(log.SceneInits, 1);
		SWIM_CHECK_EQUAL(log.Count("playing.awake"), 1);
		SWIM_CHECK(engine.Tick(10));
		SWIM_CHECK_EQUAL(log.Count("playing.init"), 1);
		SWIM_CHECK_EQUAL(log.PlayingUpdates, 10);
		SWIM_CHECK_EQUAL(log.PlayingFixed, 10); // 60 Hz frames at a 60 Hz fixed rate.
		SWIM_CHECK_NEAR(log.LastPlayingDelta, 1.0 / 60.0, 1e-9);
		SWIM_CHECK_EQUAL(log.Count("playing.exit"), 0);
	}
	// Engine exit: scene Exit once, then every behaviour's Exit once.
	SWIM_CHECK_EQUAL(log.SceneExits, 1);
	SWIM_CHECK_EQUAL(log.Count("playing.exit"), 1);
	SWIM_CHECK_EQUAL(log.Count("always.exit"), 1);
	SWIM_CHECK_EQUAL(log.Count("realtime.exit"), 1);
}

SWIM_TEST("Engine.SceneRuntime", "PauseFreezesPlayingBehavioursAndStepAdvancesOneTick")
{
	ProbeLog log;
	Swim::Tests::HeadlessEngine engine(
		[&log](Engine::SceneSystem& scenes)
		{
			RegisterProbeScenes(scenes, log);
		});
	SWIM_REQUIRE(engine.Started());
	SWIM_REQUIRE(engine.Tick(2));

	SWIM_CHECK(engine.Command("pause"));
	SWIM_CHECK(engine->GetEngineState() == EngineState::Paused);
	SWIM_CHECK_EQUAL(log.Count("playing.pause"), 1);
	SWIM_CHECK_EQUAL(log.Count("always.pause"), 1);
	SWIM_REQUIRE_EQUAL(log.SceneTransitions.size(), std::size_t{ 1 });
	SWIM_CHECK(log.SceneTransitions.back() == std::make_pair(EngineState::Playing, EngineState::Paused));

	const int updates = log.PlayingUpdates;
	const int fixed = log.PlayingFixed;
	const int always = log.AlwaysUpdates;
	SWIM_REQUIRE(engine.Tick(5));
	SWIM_CHECK_EQUAL(log.PlayingUpdates, updates);
	SWIM_CHECK_EQUAL(log.PlayingFixed, fixed);
	SWIM_CHECK_EQUAL(log.AlwaysUpdates, always + 5);
	SWIM_CHECK_NEAR(log.LastAlwaysDelta, 0.0, 1e-12);		  // Simulation time is frozen...
	SWIM_CHECK_NEAR(log.LastRealTimeDelta, 1.0 / 60.0, 1e-9); // ...wall-clock time is not.

	// One single step: exactly one fixed tick and one Update run as Playing.
	SWIM_CHECK(engine.Command("step"));
	SWIM_REQUIRE(engine.Tick(3));
	SWIM_CHECK_EQUAL(log.PlayingFixed, fixed + 1);
	SWIM_CHECK_EQUAL(log.PlayingUpdates, updates + 1);
	SWIM_CHECK(engine->GetEngineState() == EngineState::Paused);

	SWIM_CHECK(engine.Command("step 3"));
	SWIM_REQUIRE(engine.Tick(5));
	SWIM_CHECK_EQUAL(log.PlayingFixed, fixed + 4);

	SWIM_CHECK(engine.Command("resume"));
	SWIM_CHECK_EQUAL(log.Count("playing.resume"), 1);
	SWIM_REQUIRE(engine.Tick(2));
	SWIM_CHECK_EQUAL(log.PlayingFixed, fixed + 6);
}

SWIM_TEST("Engine.SceneRuntime", "TimeScaleSlowsSimulationButNotRealTimeBehaviours")
{
	ProbeLog log;
	Swim::Tests::HeadlessEngine engine(
		[&log](Engine::SceneSystem& scenes)
		{
			RegisterProbeScenes(scenes, log);
		});
	SWIM_REQUIRE(engine.Started());
	SWIM_CHECK(engine.Command("timescale 0.5"));
	const int fixed = log.PlayingFixed;
	SWIM_REQUIRE(engine.Tick(10));
	SWIM_CHECK_EQUAL(log.PlayingFixed, fixed + 5);
	SWIM_CHECK_NEAR(log.LastPlayingDelta, 0.5 / 60.0, 1e-9);
	SWIM_CHECK_NEAR(log.LastRealTimeDelta, 1.0 / 60.0, 1e-9);
}

SWIM_TEST("Engine.SceneRuntime", "StopResetsTheSceneAndPlayStartsFresh")
{
	ProbeLog log;
	Swim::Tests::HeadlessEngine engine(
		[&log](Engine::SceneSystem& scenes)
		{
			RegisterProbeScenes(scenes, log);
		});
	SWIM_REQUIRE(engine.Started());
	SWIM_REQUIRE(engine.Tick(3));

	// Gameplay changes the scene...
	auto& scene = engine.Scene();
	const std::size_t initialCount = scene.GetEntityCount();
	scene.DestroyEntity(scene.FindByName("Crate 0"));
	SWIM_CHECK_EQUAL(scene.CountWithTag(CrateTag), std::size_t{ 2 });

	SWIM_CHECK(engine.Command("stop"));
	SWIM_CHECK_EQUAL(log.Count("playing.stop"), 1);
	SWIM_REQUIRE(engine.Tick(1));
	// ...Stop restored it (Exit + Init at the start of the next frame).
	SWIM_CHECK_EQUAL(log.SceneExits, 1);
	SWIM_CHECK_EQUAL(log.SceneInits, 2);
	SWIM_CHECK_EQUAL(log.SceneAwakes, 1);
	SWIM_CHECK_EQUAL(engine->GetSceneSystem()->GetReloadCount(), std::uint64_t{ 1 });
	SWIM_CHECK_EQUAL(engine.Scene().GetEntityCount(), initialCount);
	SWIM_CHECK_EQUAL(engine.Scene().CountWithTag(CrateTag), std::size_t{ 3 });
	SWIM_CHECK_EQUAL(log.Count("playing.exit"), 1);

	// Stopped: only the all-state behaviours run.
	const int updates = log.PlayingUpdates;
	SWIM_REQUIRE(engine.Tick(2));
	SWIM_CHECK_EQUAL(log.PlayingUpdates, updates);

	SWIM_CHECK(engine.Command("play"));
	SWIM_REQUIRE(engine.Tick(2));
	SWIM_CHECK_EQUAL(log.PlayingUpdates, updates + 2);
	SWIM_CHECK_EQUAL(log.Count("playing.init"), 2);
}

SWIM_TEST("Engine.SceneRuntime", "EnginesMayStartStoppedOrPaused")
{
	ProbeLog log;
	Swim::Tests::HeadlessEngine engine(
		[&log](Engine::SceneSystem& scenes)
		{
			RegisterProbeScenes(scenes, log);
		},
		"", EngineState::Paused);
	SWIM_REQUIRE(engine.Started());
	SWIM_CHECK(engine->GetEngineState() == EngineState::Paused);
	SWIM_REQUIRE(engine.Tick(3));
	SWIM_CHECK_EQUAL(log.PlayingUpdates, 0);
	SWIM_CHECK_EQUAL(log.AlwaysUpdates, 3);
	SWIM_CHECK(log.SceneTransitions.empty()); // The initial state is not a transition.
}

SWIM_TEST("Engine.SceneRuntime", "SceneSwitchesAreDeferredToTheNextFrame")
{
	ProbeLog log;
	Swim::Tests::HeadlessEngine engine(
		[&log](Engine::SceneSystem& scenes)
		{
			RegisterProbeScenes(scenes, log);
		});
	SWIM_REQUIRE(engine.Started());
	const auto names = engine->GetSceneSystem()->GetSceneNames();
	SWIM_CHECK_EQUAL(names.size(), std::size_t{ 2 });
	SWIM_REQUIRE(engine.Tick(1)); // Behaviours Init on their first update (and only inited ones get Exit).

	SWIM_CHECK(engine.Command("scene Empty"));
	SWIM_CHECK_EQUAL(engine->GetSceneSystem()->GetActiveSceneName(), std::string("Probe"));
	SWIM_REQUIRE(engine.Tick(1));
	SWIM_CHECK_EQUAL(engine->GetSceneSystem()->GetActiveSceneName(), std::string("Empty"));
	SWIM_CHECK_EQUAL(log.SceneExits, 1);
	SWIM_CHECK_EQUAL(log.Count("playing.exit"), 1);

	SWIM_CHECK(engine.Command("scene Probe"));
	SWIM_REQUIRE(engine.Tick(1));
	SWIM_CHECK_EQUAL(log.SceneAwakes, 1); // Awake once per scene instance.
	SWIM_CHECK_EQUAL(log.SceneInits, 2);
	SWIM_CHECK_EQUAL(engine.Scene().CountWithTag(CrateTag), std::size_t{ 3 });
	SWIM_CHECK(!engine.Command("no.such.command"));
}

SWIM_TEST("Engine.SceneRuntime", "TagsNamesAndTheTagIndexStayConsistent")
{
	ProbeLog log;
	Swim::Tests::HeadlessEngine engine(
		[&log](Engine::SceneSystem& scenes)
		{
			RegisterProbeScenes(scenes, log);
		});
	SWIM_REQUIRE(engine.Started());
	auto& scene = engine.Scene();

	SWIM_CHECK_EQUAL(scene.CountWithTag(CrateTag), std::size_t{ 3 });
	SWIM_CHECK_EQUAL(scene.CountWithTag(Engine::Tags::Static), std::size_t{ 3 });
	SWIM_CHECK_EQUAL(scene.GetTagRegistry().GetName(CrateTag), std::string_view("Test.Crate"));

	const entt::entity crate = scene.FindByName("Crate 1");
	SWIM_REQUIRE(crate != entt::null);
	SWIM_CHECK(scene.HasTag(crate, CrateTag));
	SWIM_CHECK(scene.RemoveTag(crate, CrateTag));
	SWIM_CHECK(!scene.RemoveTag(crate, CrateTag));
	SWIM_CHECK(!scene.HasTag(crate, CrateTag));
	SWIM_CHECK_EQUAL(scene.CountWithTag(CrateTag), std::size_t{ 2 });
	SWIM_CHECK(scene.AddTag(crate, CrateTag));
	SWIM_CHECK(!scene.AddTag(crate, CrateTag));
	SWIM_CHECK_EQUAL(scene.CountWithTag(CrateTag), std::size_t{ 3 });

	int visited = 0;
	scene.ForEachWithTag(CrateTag,
		[&](entt::entity entity)
		{
			++visited;
			scene.DestroyEntity(entity); // Mutating while iterating a snapshot is fine.
		});
	SWIM_CHECK_EQUAL(visited, 3);
	SWIM_CHECK_EQUAL(scene.CountWithTag(CrateTag), std::size_t{ 0 });
	SWIM_CHECK(scene.FindFirstWithTag(CrateTag) == entt::null);
	SWIM_CHECK(scene.FindByName("Crate 1") == entt::null);

	const entt::entity unnamed = scene.CreateEntity();
	SWIM_CHECK(scene.GetEntityName(unnamed).rfind("Entity ", 0) == 0);
	scene.SetEntityName(unnamed, "Renamed");
	SWIM_CHECK(scene.FindByName("Renamed") == unnamed);
}

SWIM_TEST("Engine.SceneRuntime", "CommandBufferDefersStructuralChanges")
{
	ProbeLog log;
	Swim::Tests::HeadlessEngine engine(
		[&log](Engine::SceneSystem& scenes)
		{
			RegisterProbeScenes(scenes, log);
		});
	SWIM_REQUIRE(engine.Started());
	auto& scene = engine.Scene();
	const std::size_t before = scene.GetEntityCount();

	auto& commands = scene.GetCommandBuffer();
	commands.Create(
		[](Engine::Scene& owner, entt::entity entity)
		{
			owner.SetEntityName(entity, "Deferred");
			owner.AddTag(entity, CrateTag);
		});
	const entt::entity first = scene.FindByName("Crate 0");
	commands.Destroy(first);
	commands.SetName(scene.FindByName("Crate 2"), "Crate two");
	SWIM_CHECK_EQUAL(commands.GetPendingCount(), std::size_t{ 3 });
	SWIM_CHECK_EQUAL(scene.GetEntityCount(), before); // Nothing happened yet.

	SWIM_REQUIRE(engine.Tick(1));
	SWIM_CHECK_EQUAL(commands.GetPendingCount(), std::size_t{ 0 });
	SWIM_CHECK_EQUAL(scene.GetEntityCount(), before);
	SWIM_CHECK(scene.FindByName("Deferred") != entt::null);
	SWIM_CHECK(!scene.IsValid(first));
	SWIM_CHECK(scene.FindByName("Crate two") != entt::null);
	SWIM_CHECK_EQUAL(scene.CountWithTag(CrateTag), std::size_t{ 3 });
}

SWIM_TEST("Engine.SceneRuntime", "PhysicsBodiesFallCollideAndCanBeRaycast")
{
	ProbeLog log;
	Swim::Tests::HeadlessEngine engine(
		[&log](Engine::SceneSystem& scenes)
		{
			RegisterProbeScenes(scenes, log);
		});
	SWIM_REQUIRE(engine.Started());
	auto& scene = engine.Scene();

	const entt::entity floor = scene.CreateEntity("Floor");
	scene.AddComponent<Engine::Transform>(floor, Engine::Transform({ 0, -0.5f, 0 }, glm::vec3(1.0f)));
	Game::AddBoxBody(scene, floor, Engine::RigidbodyType::Static, { 10.0f, 0.5f, 10.0f });

	const entt::entity ball = scene.CreateEntity("Ball");
	scene.AddComponent<Engine::Transform>(ball, Engine::Transform({ 0, 3.0f, 0 }, glm::vec3(1.0f)));
	Game::AddSphereBody(scene, ball, Engine::RigidbodyType::Dynamic, 0.5f, 1.0f);
	scene.EmplaceBehavior<Probe>(ball, &log, ProbeKind::Playing)->EnableCollisionCallBacks();

	SWIM_REQUIRE(engine.Tick(90)); // 1.5 s: falls 2.5 m and settles.
	const float y = scene.GetRegistry().get<Engine::Transform>(ball).GetPosition().y;
	SWIM_CHECK_NEAR(y, 0.5f, 0.1f);
	SWIM_CHECK(log.CollisionsEntered >= 1);
	SWIM_CHECK(scene.GetPhysicsStepCount() >= 90);

	const auto hit = scene.Raycast({ 0, 10, 0 }, { 0, -1, 0 }, 100.0f);
	SWIM_REQUIRE(hit.has_value());
	SWIM_CHECK(hit->Entity == ball);
	SWIM_CHECK_NEAR(hit->Distance, 10.0f - (y + 0.5f), 0.05f);
	SWIM_CHECK_NEAR(hit->Normal.y, 1.0f, 1e-3f);

	const auto side = scene.Raycast({ 5, 10, 5 }, { 0, -1, 0 }, 100.0f);
	SWIM_REQUIRE(side.has_value());
	SWIM_CHECK(side->Entity == floor);
	SWIM_CHECK(!scene.Raycast({ 50, 10, 50 }, { 0, -1, 0 }, 100.0f).has_value());

	// Paused: bodies hold still.
	SWIM_CHECK(engine.Command("pause"));
	const std::uint64_t steps = scene.GetPhysicsStepCount();
	SWIM_REQUIRE(engine.Tick(10));
	SWIM_CHECK_EQUAL(scene.GetPhysicsStepCount(), steps);
}

// Collider sizes are mesh-local and follow Transform scale (a unit sphere mesh at scale 0.4
// rests with its centre 0.2 m above the floor), and an initial velocity set after the
// Rigidbody component was added (which creates the body at once) still launches it.
SWIM_TEST("Engine.SceneRuntime", "ScaledCollidersRestOnTheirSurfaceAndInitialVelocityLaunches")
{
	ProbeLog log;
	Swim::Tests::HeadlessEngine engine(
		[&log](Engine::SceneSystem& scenes)
		{
			RegisterProbeScenes(scenes, log);
		});
	SWIM_REQUIRE(engine.Started());
	auto& scene = engine.Scene();

	const entt::entity floor = scene.CreateEntity("Floor");
	scene.AddComponent<Engine::Transform>(floor, Engine::Transform({ 0, -0.5f, 0 }, glm::vec3(40.0f, 1.0f, 40.0f)));
	Game::AddBoxBody(scene, floor, Engine::RigidbodyType::Static, glm::vec3(0.5f));

	const entt::entity resting = scene.CreateEntity("Resting");
	scene.AddComponent<Engine::Transform>(resting, Engine::Transform({ -3.0f, 1.0f, 0 }, glm::vec3(0.4f)));
	Game::AddSphereBody(scene, resting, Engine::RigidbodyType::Dynamic, 0.5f, 1.0f);

	const entt::entity fired = scene.CreateEntity("Fired");
	scene.AddComponent<Engine::Transform>(fired, Engine::Transform({ 3.0f, 1.0f, 0 }, glm::vec3(0.4f)));
	Game::AddSphereBody(scene, fired, Engine::RigidbodyType::Dynamic, 0.5f, 1.0f);
	scene.GetRegistry().get<Engine::Rigidbody>(fired).SetInitialLinearVelocity({ 0.0f, 0.0f, 12.0f });

	SWIM_REQUIRE(engine.Tick(20));
	const glm::vec3 flight = scene.GetRegistry().get<Engine::Transform>(fired).GetPosition();
	SWIM_CHECK(flight.z > 2.0f);
	SWIM_REQUIRE(engine.Tick(100));
	const float y = scene.GetRegistry().get<Engine::Transform>(resting).GetPosition().y;
	SWIM_CHECK_NEAR(y, 0.2f, 0.03f);
}
