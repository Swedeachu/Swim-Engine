#pragma once

#include "Engine/Systems/Physics/IPhysicsBackend.h"

#include "PxPhysicsAPI.h"
#include "extensions/PxDefaultCpuDispatcher.h"

#include <memory>

namespace Engine
{

	class PhysXBackend final : public IPhysicsBackend
	{

	public:

		PhysXBackend() = default;
		~PhysXBackend() override;

		bool Initialize(unsigned int workerThreads) override;
		void Shutdown() override;
		std::unique_ptr<IPhysicsWorldBackend> CreateWorld(const PhysicsWorldDesc& desc) override;
		const char* GetName() const override { return "PhysX"; }

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
		bool extensionsInitialized = false;

	};

}
