#include "JoltBackend.h"
#include "JoltWorldBackend.h"

#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <iostream>
#include <mutex>

namespace Engine
{

	namespace
	{

		std::mutex JoltRuntimeMutex;
		unsigned int JoltRuntimeReferences = 0;

		bool AcquireJoltRuntime()
		{
			std::scoped_lock lock(JoltRuntimeMutex);

			if (JoltRuntimeReferences == 0)
			{
				JPH::RegisterDefaultAllocator();
				JPH::Factory::sInstance = new JPH::Factory();
				JPH::RegisterTypes();
			}

			JoltRuntimeReferences++;
			return true;
		}

		void ReleaseJoltRuntime()
		{
			std::scoped_lock lock(JoltRuntimeMutex);
			if (JoltRuntimeReferences == 0)
			{
				return;
			}

			JoltRuntimeReferences--;
			if (JoltRuntimeReferences == 0)
			{
				JPH::UnregisterTypes();
				delete JPH::Factory::sInstance;
				JPH::Factory::sInstance = nullptr;
			}
		}

	}

	JoltBackend::~JoltBackend()
	{
		Shutdown();
	}

	bool JoltBackend::Initialize(unsigned int workerThreads)
	{
		if (jobSystem)
		{
			return true;
		}

		if (!runtimeAcquired)
		{
			if (!AcquireJoltRuntime())
			{
				std::cerr << "JoltBackend::Initialize | failed to initialize Jolt runtime\n";
				return false;
			}
			runtimeAcquired = true;
		}

		const int threadCount = static_cast<int>(std::max(1u, workerThreads));
		jobSystem = std::make_unique<JPH::JobSystemThreadPool>(
			JPH::cMaxPhysicsJobs,
			JPH::cMaxPhysicsBarriers,
			threadCount);

		return true;
	}

	void JoltBackend::Shutdown()
	{
		jobSystem.reset();

		if (runtimeAcquired)
		{
			ReleaseJoltRuntime();
			runtimeAcquired = false;
		}
	}

	std::unique_ptr<IPhysicsWorldBackend> JoltBackend::CreateWorld(const PhysicsWorldDesc& desc)
	{
		if (!jobSystem)
		{
			return nullptr;
		}

		auto world = std::make_unique<JoltWorldBackend>(*jobSystem, desc);
		if (!world->Initialize())
		{
			return nullptr;
		}
		return world;
	}

}
