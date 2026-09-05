#pragma once

#include "Engine/Systems/Physics/Backends/Jolt/Filters/JoltBroadPhaseLayerInterface.h"
#include "Engine/Systems/Physics/Backends/Jolt/Internal/JoltPhysicsUtils.h"
#include "Engine/Systems/Physics/Backends/Jolt/JoltWorldBackend.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>

namespace Engine
{

	using namespace JoltPhysicsDetail;

	class JoltWorldBackend::ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
	{

	public:

		explicit ObjectVsBroadPhaseFilter(const BroadPhaseLayerInterface& selectedLayerInterface)
			: layerInterface(selectedLayerInterface)
		{}

		bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhaseLayer) const override
		{
			const BroadPhaseLayerInterface::RegisteredLayer* registeredLayer = layerInterface.Get(layer);
			if (!registeredLayer)
			{
				return false;
			}

			return registeredLayer->IsMoving || broadPhaseLayer == BroadPhaseLayers::Moving;
		}

	private:

		const BroadPhaseLayerInterface& layerInterface;

	};

} // namespace Engine
