#pragma once

#include "Engine/Systems/Physics/Backends/PhysX/Internal/PhysXPhysicsUtils.h"
#include "Engine/Systems/Physics/PhysicsTypes.h"

#include "PxPhysicsAPI.h"

namespace Engine
{

	using namespace PhysXPhysicsDetail;

		class LayerQueryFilter final : public physx::PxQueryFilterCallback
		{

		public:

			LayerQueryFilter(CollisionLayer selectedFilter, physx::PxQueryHitType::Enum selectedHitType)
				: filter(selectedFilter), hitType(selectedHitType)
			{}

			physx::PxQueryHitType::Enum preFilter(
				const physx::PxFilterData& filterData,
				const physx::PxShape* shape,
				const physx::PxRigidActor* actor,
				physx::PxHitFlags& queryFlags) override
			{
				(void)filterData;
				(void)actor;
				(void)queryFlags;

				if (!shape || !LayersMatch(filter, shape->getQueryFilterData()))
				{
					return physx::PxQueryHitType::eNONE;
				}

				return hitType;
			}

			physx::PxQueryHitType::Enum postFilter(
				const physx::PxFilterData& filterData,
				const physx::PxQueryHit& hit,
				const physx::PxShape* shape,
				const physx::PxRigidActor* actor) override
			{
				(void)filterData;
				(void)hit;
				(void)shape;
				(void)actor;
				return hitType;
			}

		private:

			CollisionLayer filter{};
			physx::PxQueryHitType::Enum hitType = physx::PxQueryHitType::eBLOCK;

		};

} // namespace Engine
