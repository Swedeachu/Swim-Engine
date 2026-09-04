#include "PhysicsSystem.h"

#include <utility>

#include <algorithm>
#include <iostream>
#include <thread>

namespace Engine
{

	PhysicsSystem::PhysicsSystem(std::unique_ptr<IPhysicsBackend> selectedBackend)
		: backend(std::move(selectedBackend))
	{}

	PhysicsSystem::~PhysicsSystem()
	{
		Exit();
	}

	int PhysicsSystem::Awake()
	{
		return 0;
	}

	int PhysicsSystem::Init()
	{
		if (initialized)
		{
			return 0;
		}

		if (!backend)
		{
			std::cerr << "PhysicsSystem::Init | no physics backend was provided\n";
			return 1;
		}

		unsigned int threads = dispatcherThreads;

		if (threads == 0)
		{
			const unsigned int hc = std::thread::hardware_concurrency();
			threads = hc <= 2 ? 1u : std::max(1u, (hc - 1u) / 2u);
		}

		if (!backend->Initialize(threads))
		{
			std::cerr << "PhysicsSystem::Init | failed to initialize " << backend->GetName() << " backend\n";
			return 2;
		}

		std::cout << "Starting " << backend->GetName() << " physics with " << threads << " worker thread(s)\n";
		initialized = true;
		return 0;
	}

	std::unique_ptr<PhysicsWorld> PhysicsSystem::CreateWorld(const PhysicsWorldDesc& desc)
	{
		if (!initialized || !backend)
		{
			return nullptr;
		}

		std::unique_ptr<IPhysicsWorldBackend> worldBackend = backend->CreateWorld(desc);
		if (!worldBackend)
		{
			return nullptr;
		}

		return std::make_unique<PhysicsWorld>(std::move(worldBackend));
	}

	const char* PhysicsSystem::GetBackendName() const
	{
		return backend ? backend->GetName() : "None";
	}

	int PhysicsSystem::Exit()
	{
		if (!initialized)
		{
			return 0;
		}

		if (backend)
		{
			backend->Shutdown();
		}

		initialized = false;
		return 0;
	}

} // namespace Engine
