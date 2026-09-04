#pragma once

#include <memory>

#include "PxPhysicsAPI.h"
#include "extensions/PxDefaultCpuDispatcher.h"
#include "Engine/EngineState.h"

namespace Engine
{

	class Scene;

	class PhysicsSystem : public Machine
	{

	public:

		int Awake() override;
		int Init() override;
		// Physics traversal is scene-explicit. The legacy Machine frame hooks are intentionally
		// not used so PhysicsSystem never discovers an application-designated active scene.
		void UpdateScene(Scene& scene, double dt);
		void FixedUpdateScene(Scene& scene, unsigned int tickThisSecond);
		int Exit() override;

		physx::PxFoundation* GetFoundation() const { return foundation.get(); }
		physx::PxPhysics* GetPxPhysics() const { return physics.get(); }

		// Return the base interface, but we own the concrete default dispatcher.
		physx::PxCpuDispatcher* GetCpuDispatcher() const { return dispatcher.get(); }

		void SetServices(const EngineState* state)
		{
			engineState = state;
		}

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

		const EngineState* engineState = nullptr;

		// Time since last fixed tick (for render interpolation alpha).
		double timeSinceLastTick = 0.0;

	};

} // namespace Engine
