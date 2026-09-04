#pragma once

#include <memory>

namespace Engine
{

	class IPhysicsBackend;

	std::unique_ptr<IPhysicsBackend> CreateJoltBackend();

}
