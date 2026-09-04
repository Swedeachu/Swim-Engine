#pragma once

#include "Engine/Machine.h"
#include "IPhysicsBackend.h"
#include "PhysicsWorld.h"

#include <memory>

namespace Engine
{

	class PhysicsSystem : public Machine
	{

	public:

		explicit PhysicsSystem(std::unique_ptr<IPhysicsBackend> backend);
		~PhysicsSystem() override;

		int Awake() override;
		int Init() override;
		int Exit() override;

		std::unique_ptr<PhysicsWorld> CreateWorld(const PhysicsWorldDesc& desc = {});

		const char* GetBackendName() const;


		float GetFixedDeltaSeconds() const { return fixedDeltaSeconds; }
		void SetFixedDeltaSeconds(float dt) { fixedDeltaSeconds = dt; }
		void SetDispatcherThreads(unsigned int threads) { dispatcherThreads = threads; }

	private:

		std::unique_ptr<IPhysicsBackend> backend;
		unsigned int dispatcherThreads = 0; // 0 => automatically determined

		// Kept in sync with the engine's tick rate via SetFixedDeltaSeconds(), by default it is 60 Hz.
		float fixedDeltaSeconds = 1.0f / 60.0f;

		bool initialized = false;

	};

} // namespace Engine
