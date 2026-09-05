#pragma once

#include "Engine/Systems/Physics/Backends/Jolt/Filters/JoltBroadPhaseLayerInterface.h"
#include "Engine/Systems/Physics/Backends/Jolt/Internal/JoltPhysicsUtils.h"
#include "Engine/Systems/Physics/Backends/Jolt/JoltWorldBackend.h"

#include <Jolt/Jolt.h>

namespace Engine
{

	using namespace JoltPhysicsDetail;

	class JoltWorldBackend::ObjectPairFilter final : public JPH::ObjectLayerPairFilter
	{

	public:

		explicit ObjectPairFilter(const BroadPhaseLayerInterface& selectedLayerInterface)
			: layerInterface(selectedLayerInterface)
		{}

		bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
		{
			const BroadPhaseLayerInterface::RegisteredLayer* registeredA = layerInterface.Get(a);
			const BroadPhaseLayerInterface::RegisteredLayer* registeredB = layerInterface.Get(b);
			if (!registeredA || !registeredB || (!registeredA->IsMoving && !registeredB->IsMoving))
			{
				return false;
			}

			return LayersMatch(registeredA->Collision, registeredB->Collision);
		}

	private:

		const BroadPhaseLayerInterface& layerInterface;

	};

} // namespace Engine
