#pragma once

#include <memory>

#include "PxPhysicsAPI.h"
#include "extensions/PxDefaultCpuDispatcher.h"

namespace Engine
{

	class Scene;

	class PhysicsSystem : public Machine
	{

	public:

		int Awake() override;
		int Init() override;
		void Update(double dt) override;
		void FixedUpdate(unsigned int tickThisSecond) override;
		int Exit() override;

		physx::PxFoundation* GetFoundation() const { return foundation.get(); }
		physx::PxPhysics* GetPxPhysics() const { return physics.get(); }

		// Return the base interface, but we own the concrete default dispatcher.
		physx::PxCpuDispatcher* GetCpuDispatcher() const { return dispatcher.get(); }

		float GetFixedDeltaSeconds() const { return fixedDeltaSeconds; }
		void SetFixedDeltaSeconds(float dt) { fixedDeltaSeconds = dt; }
		void SetDispatcherThreads(unsigned int threads) { dispatcherThreads = threads; }

	private:

		struct PxReleaser
		{
			template<typename T>
			void operator()(T* ptr) const
			{
				if (ptr)
				{
					ptr->release();
				}
			}
		};

		physx::PxDefaultAllocator allocator;
		physx::PxDefaultErrorCallback errorCallback;

		std::unique_ptr<physx::PxFoundation, PxReleaser> foundation;
		std::unique_ptr<physx::PxPhysics, PxReleaser> physics;
		std::unique_ptr<physx::PxDefaultCpuDispatcher, PxReleaser> dispatcher;

		unsigned int dispatcherThreads = 0; // 0 => automatically determined 

		// Kept in sync with the engine's tick rate via SetFixedDeltaSeconds(), by default it is 60 Hz
		float fixedDeltaSeconds = 1.0f / 60.0f; 

		// Time since last fixed tick (for render interpolation alpha).
		double timeSinceLastTick = 0.0;

	};

} // namespace Engine
