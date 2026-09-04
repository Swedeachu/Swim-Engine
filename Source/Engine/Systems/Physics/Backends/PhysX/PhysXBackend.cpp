#include "PhysXBackend.h"
#include "PhysXWorldBackend.h"

#include <iostream>

namespace Engine
{

	PhysXBackend::~PhysXBackend()
	{
		Shutdown();
	}

	bool PhysXBackend::Initialize(unsigned int workerThreads)
	{
		if (physics)
		{
			return true;
		}

		physx::PxFoundation* createdFoundation = PxCreateFoundation(PX_PHYSICS_VERSION, allocator, errorCallback);
		if (!createdFoundation)
		{
			std::cerr << "PhysXBackend::Initialize | PxCreateFoundation failed\n";
			return false;
		}
		foundation.reset(createdFoundation);

		physx::PxTolerancesScale scale;
		physx::PxPhysics* createdPhysics = PxCreatePhysics(PX_PHYSICS_VERSION, *foundation, scale, false, nullptr);
		if (!createdPhysics)
		{
			std::cerr << "PhysXBackend::Initialize | PxCreatePhysics failed\n";
			Shutdown();
			return false;
		}
		physics.reset(createdPhysics);

		if (!PxInitExtensions(*physics, nullptr))
		{
			std::cerr << "PhysXBackend::Initialize | PxInitExtensions failed\n";
			Shutdown();
			return false;
		}
		extensionsInitialized = true;

		physx::PxDefaultCpuDispatcher* createdDispatcher = physx::PxDefaultCpuDispatcherCreate(workerThreads);
		if (!createdDispatcher)
		{
			std::cerr << "PhysXBackend::Initialize | PxDefaultCpuDispatcherCreate failed\n";
			Shutdown();
			return false;
		}
		dispatcher.reset(createdDispatcher);

		return true;
	}

	void PhysXBackend::Shutdown()
	{
		dispatcher.reset();

		if (extensionsInitialized && physics)
		{
			PxCloseExtensions();
			extensionsInitialized = false;
		}

		physics.reset();
		foundation.reset();
	}

	std::unique_ptr<IPhysicsWorldBackend> PhysXBackend::CreateWorld(const PhysicsWorldDesc& desc)
	{
		if (!physics || !dispatcher)
		{
			return nullptr;
		}

		auto world = std::make_unique<PhysXWorldBackend>(*physics, *dispatcher, desc);
		if (!world->Initialize())
		{
			return nullptr;
		}
		return world;
	}

}
