#pragma once

#include "Engine/Systems/Physics/IPhysicsBackend.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystemThreadPool.h>

#include <memory>

namespace Engine
{

	class JoltBackend final : public IPhysicsBackend
	{

	public:

		JoltBackend() = default;
		~JoltBackend() override;

		bool Initialize(unsigned int workerThreads) override;
		void Shutdown() override;
		std::unique_ptr<IPhysicsWorldBackend> CreateWorld(const PhysicsWorldDesc& desc) override;
		const char* GetName() const override { return "Jolt"; }

	private:

		std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;
		bool runtimeAcquired = false;

	};

}
