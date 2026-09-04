#include "JoltBackendFactory.h"
#include "JoltBackend.h"

namespace Engine
{

	std::unique_ptr<IPhysicsBackend> CreateJoltBackend()
	{
		return std::make_unique<JoltBackend>();
	}

}
