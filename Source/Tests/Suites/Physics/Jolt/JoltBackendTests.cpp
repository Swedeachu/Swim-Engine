#include "Engine/Systems/Physics/Backends/Jolt/JoltBackendFactory.h"
#include "Tests/Fixtures/PhysicsBackendContract.h"
#include "Tests/Framework/Test.h"

#include <memory>
#include <string>

namespace
{

	Engine::IPhysicsBackend* SharedJoltBackend()
	{
		static std::unique_ptr<Engine::IPhysicsBackend> backend = Engine::CreateJoltBackend();
		return backend.get();
	}

}

SWIM_TEST("Physics.JoltBackend", "FactoryProducesTheNamedBackend")
{
	std::unique_ptr<Engine::IPhysicsBackend> backend = Engine::CreateJoltBackend();
	SWIM_REQUIRE(backend != nullptr);
	SWIM_CHECK_EQUAL(std::string(backend->GetName()), std::string("Jolt"));
}


SWIM_TEST("Physics.JoltBackend", "MultipleBackendInstancesShareTheRuntime")
{
	std::unique_ptr<Engine::IPhysicsBackend> first = Engine::CreateJoltBackend();
	std::unique_ptr<Engine::IPhysicsBackend> second = Engine::CreateJoltBackend();
	SWIM_REQUIRE(first != nullptr);
	SWIM_REQUIRE(second != nullptr);
	SWIM_REQUIRE(first->Initialize(1));
	SWIM_REQUIRE(second->Initialize(1));

	std::unique_ptr<Engine::IPhysicsWorldBackend> firstWorld = first->CreateWorld(Engine::PhysicsWorldDesc{});
	std::unique_ptr<Engine::IPhysicsWorldBackend> secondWorld = second->CreateWorld(Engine::PhysicsWorldDesc{});
	SWIM_REQUIRE(firstWorld != nullptr);
	SWIM_REQUIRE(secondWorld != nullptr);

	secondWorld.reset();
	second->Shutdown();

	std::unique_ptr<Engine::IPhysicsWorldBackend> survivingWorld = first->CreateWorld(Engine::PhysicsWorldDesc{});
	SWIM_CHECK(survivingWorld != nullptr);
}

SWIM_TEST("Physics.JoltBackend", "WorldLifecycleContract")
{
	Engine::IPhysicsBackend* backend = SharedJoltBackend();
	SWIM_REQUIRE(backend != nullptr);
	Engine::Tests::RunPhysicsWorldLifecycleContract(*backend);
}

SWIM_TEST("Physics.JoltBackend", "SceneQueryContract")
{
	Engine::IPhysicsBackend* backend = SharedJoltBackend();
	SWIM_REQUIRE(backend != nullptr);
	Engine::Tests::RunPhysicsSceneQueryContract(*backend);
}

SWIM_TEST("Physics.JoltBackend", "SimulationContract")
{
	Engine::IPhysicsBackend* backend = SharedJoltBackend();
	SWIM_REQUIRE(backend != nullptr);
	Engine::Tests::RunPhysicsSimulationContract(*backend);
}

SWIM_TEST("Physics.JoltBackend", "TriggerContract")
{
	Engine::IPhysicsBackend* backend = SharedJoltBackend();
	SWIM_REQUIRE(backend != nullptr);
	Engine::Tests::RunPhysicsTriggerContract(*backend);
}

SWIM_TEST("Physics.JoltBackend", "SharedShapeContract")
{
	Engine::IPhysicsBackend* backend = SharedJoltBackend();
	SWIM_REQUIRE(backend != nullptr);
	Engine::Tests::RunPhysicsSharedShapeContract(*backend);
}

SWIM_TEST("Physics.JoltBackend", "InFlightWriteContract")
{
	Engine::IPhysicsBackend* backend = SharedJoltBackend();
	SWIM_REQUIRE(backend != nullptr);
	Engine::Tests::RunPhysicsInFlightWriteContract(*backend);
}

SWIM_TEST("Physics.JoltBackend", "ContactEventContract")
{
	Engine::IPhysicsBackend* backend = SharedJoltBackend();
	SWIM_REQUIRE(backend != nullptr);
	Engine::Tests::RunPhysicsContactEventContract(*backend);
}
