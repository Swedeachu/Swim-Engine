#include "PhysXBackendFactory.h"
#include "PhysXBackend.h"

namespace Engine
{

	std::unique_ptr<IPhysicsBackend> CreatePhysXBackend()
	{
		return std::make_unique<PhysXBackend>();
	}

}
