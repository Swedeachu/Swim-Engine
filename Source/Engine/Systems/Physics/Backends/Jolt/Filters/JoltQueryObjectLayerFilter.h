#pragma once

#include "Engine/Systems/Physics/Backends/Jolt/Filters/JoltBroadPhaseLayerInterface.h"
#include "Engine/Systems/Physics/Backends/Jolt/Internal/JoltPhysicsUtils.h"
#include "Engine/Systems/Physics/Backends/Jolt/JoltWorldBackend.h"

#include <Jolt/Jolt.h>

namespace Engine
{

	using namespace JoltPhysicsDetail;

	class JoltWorldBackend::QueryObjectLayerFilter final : public JPH::ObjectLayerFilter
	{

	public:

		QueryObjectLayerFilter(const BroadPhaseLayerInterface& selectedLayerInterface, CollisionLayer selectedFilter)
			: layerInterface(selectedLayerInterface), filter(selectedFilter)
		{}

		bool ShouldCollide(JPH::ObjectLayer layer) const override
		{
			const BroadPhaseLayerInterface::RegisteredLayer* registeredLayer = layerInterface.Get(layer);
			return registeredLayer && LayersMatch(filter, registeredLayer->Collision);
		}

	private:

		const BroadPhaseLayerInterface& layerInterface;
		CollisionLayer filter{};

	};

} // namespace Engine
