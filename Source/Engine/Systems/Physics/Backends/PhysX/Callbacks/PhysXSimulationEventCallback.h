#pragma once

#include "Engine/Systems/Physics/Backends/PhysX/PhysXWorldBackend.h"

#include "PxPhysicsAPI.h"

namespace Engine
{

	class PhysXWorldBackend::SimulationEventCallback final : public physx::PxSimulationEventCallback
	{

	public:

		explicit SimulationEventCallback(PhysXWorldBackend& selectedOwner)
			: owner(selectedOwner)
		{}

		void onConstraintBreak(physx::PxConstraintInfo* constraints, physx::PxU32 count) override
		{
			(void)constraints;
			(void)count;
		}

		void onWake(physx::PxActor** actors, physx::PxU32 count) override
		{
			(void)actors;
			(void)count;
		}

		void onSleep(physx::PxActor** actors, physx::PxU32 count) override
		{
			(void)actors;
			(void)count;
		}

		void onContact(
			const physx::PxContactPairHeader& pairHeader,
			const physx::PxContactPair* pairs,
			physx::PxU32 nbPairs) override
		{
			if (!pairs || !pairHeader.actors[0] || !pairHeader.actors[1])
			{
				return;
			}

			const BodyHandle bodyA = owner.ResolveBody(pairHeader.actors[0]);
			const BodyHandle bodyB = owner.ResolveBody(pairHeader.actors[1]);
			if (!bodyA || !bodyB)
			{
				return;
			}

			for (physx::PxU32 i = 0; i < nbPairs; ++i)
			{
				const physx::PxContactPair& pair = pairs[i];

				CollisionEvent event{};
				event.BodyA = bodyA;
				event.BodyB = bodyB;
				event.ShapeA = owner.ResolveShape(pair.shapes[0]);
				event.ShapeB = owner.ResolveShape(pair.shapes[1]);

				if (pair.events & physx::PxPairFlag::eNOTIFY_TOUCH_FOUND)
				{
					event.Type = CollisionEventType::Started;
				}
				else if (pair.events & physx::PxPairFlag::eNOTIFY_TOUCH_LOST)
				{
					event.Type = CollisionEventType::Ended;
				}
				else if (pair.events & physx::PxPairFlag::eNOTIFY_TOUCH_PERSISTS)
				{
					event.Type = CollisionEventType::Persisted;
				}
				else
				{
					continue;
				}

				if (event.Type != CollisionEventType::Ended && pair.contactCount > 0)
				{
					physx::PxContactPairPoint point{};
					if (pair.extractContacts(&point, 1) > 0)
					{
						event.Position = PhysXWorldBackend::ToGlm(point.position);
						event.Normal = PhysXWorldBackend::ToGlm(point.normal);
						event.Impulse = point.impulse.magnitude();
					}
				}

				owner.collisionEvents.push_back(event);
			}
		}

		void onTrigger(physx::PxTriggerPair* pairs, physx::PxU32 count) override
		{
			if (!pairs)
			{
				return;
			}

			for (physx::PxU32 i = 0; i < count; ++i)
			{
				const physx::PxTriggerPair& pair = pairs[i];
				if ((pair.flags & physx::PxTriggerPairFlag::eREMOVED_SHAPE_TRIGGER)
					|| (pair.flags & physx::PxTriggerPairFlag::eREMOVED_SHAPE_OTHER))
				{
					continue;
				}

				const bool entered = pair.status == physx::PxPairFlag::eNOTIFY_TOUCH_FOUND;
				const bool exited = pair.status == physx::PxPairFlag::eNOTIFY_TOUCH_LOST;
				if (!entered && !exited)
				{
					continue;
				}

				TriggerEvent event{};
				event.TriggerBody = owner.ResolveBody(pair.triggerActor);
				event.OtherBody = owner.ResolveBody(pair.otherActor);
				event.TriggerShape = owner.ResolveShape(pair.triggerShape);
				event.OtherShape = owner.ResolveShape(pair.otherShape);
				event.Entered = entered;

				if (event.TriggerBody && event.OtherBody)
				{
					owner.triggerEvents.push_back(event);
				}
			}
		}

		void onAdvance(
			const physx::PxRigidBody* const* bodyBuffer,
			const physx::PxTransform* poseBuffer,
			const physx::PxU32 count) override
		{
			(void)bodyBuffer;
			(void)poseBuffer;
			(void)count;
		}

	private:

		PhysXWorldBackend& owner;

	};

} // namespace Engine
