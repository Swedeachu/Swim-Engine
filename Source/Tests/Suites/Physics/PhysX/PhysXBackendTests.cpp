#include "Engine/Systems/Physics/Backends/PhysX/PhysXBackendFactory.h"
#include "Tests/Fixtures/PhysicsBackendContract.h"
#include "Tests/Framework/Test.h"

#include <memory>
#include <string>

namespace
{

	// PhysX owns process-wide foundation state, so the backend is created once
	// and shared. Each contract scenario still builds and tears down its own
	// world, which is the real unit under test.
	Engine::IPhysicsBackend* SharedPhysXBackend()
	{
		static std::unique_ptr<Engine::IPhysicsBackend> backend = Engine::CreatePhysXBackend();
		return backend.get();
	}

}

SWIM_TEST("Physics.PhysXBackend", "FactoryProducesTheNamedBackend")
{
	std::unique_ptr<Engine::IPhysicsBackend> backend = Engine::CreatePhysXBackend();
	SWIM_REQUIRE(backend != nullptr);
	SWIM_CHECK_EQUAL(std::string(backend->GetName()), std::string("PhysX"));
}

SWIM_TEST("Physics.PhysXBackend", "WorldLifecycleContract")
{
	Engine::IPhysicsBackend* backend = SharedPhysXBackend();
	SWIM_REQUIRE(backend != nullptr);
	Engine::Tests::RunPhysicsWorldLifecycleContract(*backend);
}

SWIM_TEST("Physics.PhysXBackend", "SceneQueryContract")
{
	Engine::IPhysicsBackend* backend = SharedPhysXBackend();
	SWIM_REQUIRE(backend != nullptr);
	Engine::Tests::RunPhysicsSceneQueryContract(*backend);
}

SWIM_TEST("Physics.PhysXBackend", "SimulationContract")
{
	Engine::IPhysicsBackend* backend = SharedPhysXBackend();
	SWIM_REQUIRE(backend != nullptr);
	Engine::Tests::RunPhysicsSimulationContract(*backend);
}

SWIM_TEST("Physics.PhysXBackend", "TriggerContract")
{
	Engine::IPhysicsBackend* backend = SharedPhysXBackend();
	SWIM_REQUIRE(backend != nullptr);
	Engine::Tests::RunPhysicsTriggerContract(*backend);
}

SWIM_TEST("Physics.PhysXBackend", "SharedShapeContract")
{
	Engine::IPhysicsBackend* backend = SharedPhysXBackend();
	SWIM_REQUIRE(backend != nullptr);
	Engine::Tests::RunPhysicsSharedShapeContract(*backend);
}

SWIM_TEST("Physics.PhysXBackend", "InFlightWriteContract")
{
	Engine::IPhysicsBackend* backend = SharedPhysXBackend();
	SWIM_REQUIRE(backend != nullptr);
	Engine::Tests::RunPhysicsInFlightWriteContract(*backend);
}

SWIM_TEST("Physics.PhysXBackend", "ContactEventContract")
{
	Engine::IPhysicsBackend* backend = SharedPhysXBackend();
	SWIM_REQUIRE(backend != nullptr);
	Engine::Tests::RunPhysicsContactEventContract(*backend);
}
