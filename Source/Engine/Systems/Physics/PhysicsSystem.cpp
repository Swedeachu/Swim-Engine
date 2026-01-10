#include "PCH.h"
#include "PhysicsSystem.h"

#include "Engine/SwimEngine.h"
#include "Engine/Systems/Scene/SceneSystem.h"
#include "Engine/Systems/Scene/Scene.h"
#include "PhysicsWorld.h"

#include <thread>

namespace Engine
{

	int PhysicsSystem::Awake()
	{
		return 0;
	}

	int PhysicsSystem::Init()
	{
		physx::PxFoundation* f = PxCreateFoundation(PX_PHYSICS_VERSION, allocator, errorCallback);
		if (!f)
		{
			std::cerr << "PhysicsSystem::Init | PxCreateFoundation failed\n";
			return 1;
		}

		foundation.reset(f);

		physx::PxTolerancesScale scale;
		physx::PxPhysics* p = PxCreatePhysics(PX_PHYSICS_VERSION, *foundation, scale, false, nullptr);
		if (!p)
		{
			std::cerr << "PhysicsSystem::Init | PxCreatePhysics failed\n";
			return 2;
		}

		physics.reset(p);

		if (!PxInitExtensions(*physics, nullptr))
		{
			std::cerr << "PhysicsSystem::Init | PxInitExtensions failed\n";
			return 3;
		}

		unsigned int threads = dispatcherThreads;

		if (threads == 0)
		{
			unsigned int hc = std::thread::hardware_concurrency();
			if (hc <= 2)
			{
				threads = 1;
			}
			else
			{
				threads = hc - 1;
			}
		}

		// We might really want to change the amount of threads to something less high as whatever hardware_concurrency returns
		physx::PxDefaultCpuDispatcher* d = physx::PxDefaultCpuDispatcherCreate(threads);
		if (!d)
		{
			std::cerr << "PhysicsSystem::Init | PxDefaultCpuDispatcherCreate failed\n";
			return 4;
		}

		dispatcher.reset(d);

		return 0;
	}

	void PhysicsSystem::Update(double dt)
	{
		auto engine = SwimEngine::GetInstance();
		if (!engine)
		{
			return;
		}

		// Interpolation only while playing (dynamic bodies).
		if (!HasAnyEngineStates(engine->GetEngineState(), EngineState::Playing))
		{
			timeSinceLastTick = 0.0;
			return;
		}

		auto& sceneSystem = engine->GetSceneSystem();
		if (!sceneSystem)
		{
			return;
		}

		std::shared_ptr<Scene>& scene = sceneSystem->GetActiveScene();
		if (!scene)
		{
			return;
		}

		PhysicsWorld* worldPtr = scene->GetPhysicsWorld();
		if (!worldPtr)
		{
			return;
		}

		timeSinceLastTick += dt;

		float alpha = 1.0f;

		if (fixedDeltaSeconds > 0.0f)
		{
			alpha = static_cast<float>(timeSinceLastTick / static_cast<double>(fixedDeltaSeconds));
		}

		if (alpha < 0.0f) { alpha = 0.0f; }
		if (alpha > 1.0f) { alpha = 1.0f; }

		worldPtr->Interpolate(alpha);
	}

	// Each tick we get the active scene's physics world and tick it
	void PhysicsSystem::FixedUpdate(unsigned int tickThisSecond)
	{
		(void)tickThisSecond;

		auto engine = SwimEngine::GetInstance();
		if (!engine)
		{
			return;
		}

		// We need to be playing
		if (!HasAnyEngineStates(engine->GetEngineState(), EngineState::Playing))
		{
			timeSinceLastTick = 0.0;
			return;
		}

		auto& sceneSystem = engine->GetSceneSystem();
		if (!sceneSystem)
		{
			return;
		}

		std::shared_ptr<Scene>& scene = sceneSystem->GetActiveScene();
		if (!scene)
		{
			return;
		}

		PhysicsWorld& world = scene->GetOrCreatePhysicsWorld(*this);

		// Guarantee we are fully snapped to last tick's target pose before the next tick begins.
		// This keeps render/physics state coherent at tick boundaries.
		world.Interpolate(1.0f);

		// Reset interpolation timer for the next tick window.
		timeSinceLastTick = 0.0;

		world.PreSimulateSync(fixedDeltaSeconds);
		world.Step(fixedDeltaSeconds);
		world.FetchResults(true);
		world.PostSimulateSync();
	}

	int PhysicsSystem::Exit()
	{
		// Extensions should be closed before physics is released.
		if (physics)
		{
			PxCloseExtensions();
		}

		// Owned resources.
		dispatcher.reset();
		physics.reset();
		foundation.reset();

		return 0;
	}

} // namespace Engine
