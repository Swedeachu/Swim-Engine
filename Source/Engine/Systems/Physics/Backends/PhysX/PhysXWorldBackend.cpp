#include "PhysXWorldBackend.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

namespace Engine
{

	namespace
	{

		bool IsFiniteVec3(const glm::vec3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		bool IsFiniteQuat(const glm::quat& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
		}

		bool IsValidPose(const PhysicsPose& pose)
		{
			if (!IsFiniteVec3(pose.Position) || !IsFiniteQuat(pose.Rotation))
			{
				return false;
			}

			const float lengthSquared = pose.Rotation.x * pose.Rotation.x
				+ pose.Rotation.y * pose.Rotation.y
				+ pose.Rotation.z * pose.Rotation.z
				+ pose.Rotation.w * pose.Rotation.w;
			return lengthSquared > 0.000001f && std::isfinite(lengthSquared);
		}

		bool IsValidDirection(const glm::vec3& direction, float maxDistance)
		{
			if (!IsFiniteVec3(direction) || !(maxDistance > 0.0f) || !std::isfinite(maxDistance))
			{
				return false;
			}

			const float lengthSquared = glm::dot(direction, direction);
			return lengthSquared > 0.000001f && std::isfinite(lengthSquared);
		}

		bool LayersMatch(const CollisionLayer& query, const physx::PxFilterData& object)
		{
			return (query.Layer & object.word1) != 0u && (object.word0 & query.Mask) != 0u;
		}

		physx::PxFilterFlags SwimSimulationFilterShader(
			physx::PxFilterObjectAttributes attributes0,
			physx::PxFilterData filterData0,
			physx::PxFilterObjectAttributes attributes1,
			physx::PxFilterData filterData1,
			physx::PxPairFlags& pairFlags,
			const void* constantBlock,
			physx::PxU32 constantBlockSize)
		{
			(void)constantBlock;
			(void)constantBlockSize;

			if ((filterData0.word0 & filterData1.word1) == 0u || (filterData1.word0 & filterData0.word1) == 0u)
			{
				return physx::PxFilterFlag::eSUPPRESS;
			}

			if (physx::PxFilterObjectIsTrigger(attributes0) || physx::PxFilterObjectIsTrigger(attributes1))
			{
				pairFlags = physx::PxPairFlag::eTRIGGER_DEFAULT;
				return physx::PxFilterFlag::eDEFAULT;
			}

			pairFlags = physx::PxPairFlag::eCONTACT_DEFAULT
				| physx::PxPairFlag::eNOTIFY_TOUCH_FOUND
				| physx::PxPairFlag::eNOTIFY_TOUCH_PERSISTS
				| physx::PxPairFlag::eNOTIFY_TOUCH_LOST
				| physx::PxPairFlag::eNOTIFY_CONTACT_POINTS;
			return physx::PxFilterFlag::eDEFAULT;
		}

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

	}

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

	PhysXWorldBackend::PhysXWorldBackend(physx::PxPhysics& physicsRef, physx::PxCpuDispatcher& dispatcherRef, PhysicsWorldDesc desc)
		: physics(physicsRef), dispatcher(dispatcherRef), worldDesc(desc)
	{}

	PhysXWorldBackend::~PhysXWorldBackend()
	{
		if (scene && simulating)
		{
			scene->fetchResults(true);
			simulating = false;
		}

		FlushPendingDestroy();

		bodies.ForEach(
			[&](BodyHandle handle, BodyRecord& record)
			{
				(void)handle;
				ReleaseActor(record.Actor);
				record.Actor = nullptr;
			});
		bodies.Reset();
		actorHandles.clear();

		shapes.ForEach(
			[&](ShapeHandle handle, physx::PxShape*& shape)
			{
				(void)handle;
				if (shape)
				{
					shape->release();
					shape = nullptr;
				}
			});
		shapes.Reset();
		shapeHandles.clear();

		materials.ForEach(
			[&](PhysicsMaterialHandle handle, physx::PxMaterial*& material)
			{
				(void)handle;
				if (material)
				{
					material->release();
					material = nullptr;
				}
			});
		materials.Reset();

		collisionEvents.clear();
		triggerEvents.clear();
		scene.reset();
		eventCallback.reset();
	}

	bool PhysXWorldBackend::Initialize()
	{
		if (scene)
		{
			return true;
		}

		eventCallback = std::make_unique<SimulationEventCallback>(*this);

		physx::PxSceneDesc desc(physics.getTolerancesScale());
		desc.gravity = ToPx(worldDesc.Gravity);
		desc.cpuDispatcher = &dispatcher;
		desc.filterShader = SwimSimulationFilterShader;
		desc.simulationEventCallback = eventCallback.get();
		if (worldDesc.EnableContinuousCollisionDetection)
		{
			desc.flags |= physx::PxSceneFlag::eENABLE_CCD;
		}

		physx::PxScene* createdScene = physics.createScene(desc);
		if (!createdScene)
		{
			eventCallback.reset();
			return false;
		}

		scene.reset(createdScene);
		return true;
	}

	PhysicsMaterialHandle PhysXWorldBackend::CreateMaterial(const PhysicsMaterialDesc& desc)
	{
		if (!(desc.StaticFriction >= 0.0f) || !(desc.DynamicFriction >= 0.0f)
			|| !(desc.Restitution >= 0.0f && desc.Restitution <= 1.0f)
			|| !std::isfinite(desc.StaticFriction) || !std::isfinite(desc.DynamicFriction) || !std::isfinite(desc.Restitution))
		{
			return {};
		}

		physx::PxMaterial* material = physics.createMaterial(desc.StaticFriction, desc.DynamicFriction, desc.Restitution);
		if (!material)
		{
			return {};
		}
		return materials.Insert(material);
	}

	void PhysXWorldBackend::DestroyMaterial(PhysicsMaterialHandle material)
	{
		physx::PxMaterial* pxMaterial = nullptr;
		if (materials.Remove(material, pxMaterial) && pxMaterial)
		{
			pxMaterial->release();
		}
	}

	bool PhysXWorldBackend::IsMaterialValid(PhysicsMaterialHandle material) const
	{
		return materials.IsValid(material);
	}

	ShapeHandle PhysXWorldBackend::CreateShape(const ShapeDesc& desc, PhysicsMaterialHandle material)
	{
		physx::PxMaterial* const* materialPtr = materials.Get(material);
		if (!materialPtr || !*materialPtr || !IsValidPose(desc.LocalPose))
		{
			return {};
		}

		physx::PxShape* shape = nullptr;

		switch (desc.Type)
		{
			case ShapeType::Box:
			{
				const glm::vec3 extents = desc.Box.HalfExtents;
				if (!IsFiniteVec3(extents) || !(extents.x > 0.0f) || !(extents.y > 0.0f) || !(extents.z > 0.0f))
				{
					return {};
				}
				shape = physics.createShape(physx::PxBoxGeometry(extents.x, extents.y, extents.z), **materialPtr, true);
				break;
			}
			case ShapeType::Sphere:
			{
				if (!(desc.Sphere.Radius > 0.0f) || !std::isfinite(desc.Sphere.Radius))
				{
					return {};
				}
				shape = physics.createShape(physx::PxSphereGeometry(desc.Sphere.Radius), **materialPtr, true);
				break;
			}
			case ShapeType::Capsule:
			{
				if (!(desc.Capsule.Radius > 0.0f) || !(desc.Capsule.HalfHeight >= 0.0f)
					|| !std::isfinite(desc.Capsule.Radius) || !std::isfinite(desc.Capsule.HalfHeight))
				{
					return {};
				}
				shape = physics.createShape(physx::PxCapsuleGeometry(desc.Capsule.Radius, desc.Capsule.HalfHeight), **materialPtr, true);
				break;
			}
			case ShapeType::ConvexMesh:
			case ShapeType::TriangleMesh:
			{
				// Mesh cooking is intentionally an asset-compiler responsibility. The
				// generic descriptor reserves this path; backend cooked payload binding
				// lands with the collision-cooking checklist rather than synchronously
				// cooking source geometry here.
				return {};
			}
		}

		if (!shape)
		{
			return {};
		}

		physx::PxTransform localPose = ToPx(desc.LocalPose);
		if (desc.Type == ShapeType::Capsule)
		{
			const physx::PxQuat yUp(physx::PxHalfPi, physx::PxVec3(0.0f, 0.0f, 1.0f));
			localPose.q = localPose.q * yUp;
		}
		shape->setLocalPose(localPose);

		if (desc.IsTrigger)
		{
			shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, false);
			shape->setFlag(physx::PxShapeFlag::eTRIGGER_SHAPE, true);
		}
		else
		{
			shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, true);
			shape->setFlag(physx::PxShapeFlag::eTRIGGER_SHAPE, false);
		}

		const ShapeHandle handle = shapes.Insert(shape);
		shapeHandles.emplace(shape, handle);
		return handle;
	}

	void PhysXWorldBackend::DestroyShape(ShapeHandle shape)
	{
		physx::PxShape* pxShape = nullptr;
		if (shapes.Remove(shape, pxShape) && pxShape)
		{
			shapeHandles.erase(pxShape);
			pxShape->release();
		}
	}

	bool PhysXWorldBackend::IsShapeValid(ShapeHandle shape) const
	{
		return shapes.IsValid(shape);
	}

	BodyHandle PhysXWorldBackend::CreateBody(const BodyDesc& desc)
	{
		if (!scene || !IsValidPose(desc.Pose))
		{
			return {};
		}

		physx::PxShape* const* shapePtr = shapes.Get(desc.Shape);
		if (!shapePtr || !*shapePtr)
		{
			return {};
		}

		if (desc.Motion != MotionType::Static)
		{
			if (!(desc.Mass > 0.0f) || !std::isfinite(desc.Mass)
				|| !(desc.LinearDamping >= 0.0f) || !(desc.AngularDamping >= 0.0f)
				|| !std::isfinite(desc.LinearDamping) || !std::isfinite(desc.AngularDamping))
			{
				return {};
			}
		}

		if ((desc.HasInitialLinearVelocity && !IsFiniteVec3(desc.InitialLinearVelocity))
			|| (desc.HasInitialAngularVelocity && !IsFiniteVec3(desc.InitialAngularVelocity)))
		{
			return {};
		}

		physx::PxRigidActor* actor = nullptr;
		physx::PxRigidDynamic* dynamic = nullptr;
		const physx::PxTransform pose = ToPx(desc.Pose);

		if (desc.Motion == MotionType::Static)
		{
			actor = physics.createRigidStatic(pose);
		}
		else
		{
			dynamic = physics.createRigidDynamic(pose);
			if (!dynamic)
			{
				return {};
			}

			dynamic->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, desc.Motion == MotionType::Kinematic);
			dynamic->setRigidBodyFlag(physx::PxRigidBodyFlag::eENABLE_CCD,
				worldDesc.EnableContinuousCollisionDetection && desc.Motion == MotionType::Dynamic);
			dynamic->setActorFlag(physx::PxActorFlag::eDISABLE_GRAVITY, !desc.UseGravity);
			dynamic->setLinearDamping(desc.LinearDamping);
			dynamic->setAngularDamping(desc.AngularDamping);
			actor = dynamic;
		}

		if (!actor)
		{
			return {};
		}

		physx::PxFilterData filterData;
		filterData.word0 = desc.Collision.Layer;
		filterData.word1 = desc.Collision.Mask;
		(*shapePtr)->setSimulationFilterData(filterData);
		(*shapePtr)->setQueryFilterData(filterData);
		if (!actor->attachShape(**shapePtr))
		{
			std::cerr << "PhysXWorldBackend::CreateBody | attachShape failed\n";
			ReleaseActor(actor);
			return {};
		}

		if (desc.Motion == MotionType::Dynamic && dynamic)
		{
			if (!physx::PxRigidBodyExt::setMassAndUpdateInertia(*dynamic, desc.Mass))
			{
				std::cerr << "PhysXWorldBackend::CreateBody | setMassAndUpdateInertia failed\n";
			}
		}

		scene->addActor(*actor);

		if (dynamic)
		{
			if (desc.HasInitialLinearVelocity)
			{
				dynamic->setLinearVelocity(ToPx(desc.InitialLinearVelocity), true);
			}
			if (desc.HasInitialAngularVelocity)
			{
				dynamic->setAngularVelocity(ToPx(desc.InitialAngularVelocity), true);
			}

			if (desc.Motion == MotionType::Dynamic)
			{
				if (desc.StartAwake)
				{
					dynamic->wakeUp();
				}
				else
				{
					dynamic->putToSleep();
				}
			}
		}

		const BodyHandle handle = bodies.Insert(BodyRecord{ actor, desc.Shape, desc.UserData });
		actorHandles.emplace(actor, handle);
		return handle;
	}

	void PhysXWorldBackend::DestroyBody(BodyHandle body)
	{
		BodyRecord record{};
		if (!bodies.Remove(body, record) || !record.Actor)
		{
			return;
		}

		actorHandles.erase(record.Actor);

		if (simulating)
		{
			pendingDestroy.push_back(record.Actor);
			return;
		}

		ReleaseActor(record.Actor);
	}

	bool PhysXWorldBackend::IsBodyValid(BodyHandle body) const
	{
		return bodies.IsValid(body);
	}

	bool PhysXWorldBackend::SetBodyPose(BodyHandle body, const PhysicsPose& pose, bool autowake)
	{
		if (!IsValidPose(pose))
		{
			return false;
		}

		BodyRecord* record = bodies.Get(body);
		if (!record || !record->Actor)
		{
			return false;
		}

		record->Actor->setGlobalPose(ToPx(pose), autowake);
		return true;
	}

	bool PhysXWorldBackend::SetKinematicTarget(BodyHandle body, const PhysicsPose& pose)
	{
		if (!IsValidPose(pose))
		{
			return false;
		}

		BodyRecord* record = bodies.Get(body);
		physx::PxRigidDynamic* dynamic = record && record->Actor ? record->Actor->is<physx::PxRigidDynamic>() : nullptr;
		if (!dynamic || !dynamic->getRigidBodyFlags().isSet(physx::PxRigidBodyFlag::eKINEMATIC))
		{
			return false;
		}

		dynamic->setKinematicTarget(ToPx(pose));
		return true;
	}

	bool PhysXWorldBackend::GetBodyPose(BodyHandle body, PhysicsPose& pose) const
	{
		const BodyRecord* record = bodies.Get(body);
		if (!record || !record->Actor)
		{
			return false;
		}

		pose = ToEngine(record->Actor->getGlobalPose());
		return true;
	}

	bool PhysXWorldBackend::AddForce(BodyHandle body, const glm::vec3& force, ForceMode mode, bool autowake)
	{
		if (!IsFiniteVec3(force))
		{
			return false;
		}

		BodyRecord* record = bodies.Get(body);
		physx::PxRigidDynamic* dynamic = record && record->Actor ? record->Actor->is<physx::PxRigidDynamic>() : nullptr;
		if (!dynamic || dynamic->getRigidBodyFlags().isSet(physx::PxRigidBodyFlag::eKINEMATIC))
		{
			return false;
		}

		dynamic->addForce(ToPx(force), ToPx(mode), autowake);
		return true;
	}

	bool PhysXWorldBackend::SetLinearVelocity(BodyHandle body, const glm::vec3& velocity, bool autowake)
	{
		if (!IsFiniteVec3(velocity))
		{
			return false;
		}

		BodyRecord* record = bodies.Get(body);
		physx::PxRigidDynamic* dynamic = record && record->Actor ? record->Actor->is<physx::PxRigidDynamic>() : nullptr;
		if (!dynamic)
		{
			return false;
		}

		dynamic->setLinearVelocity(ToPx(velocity), autowake);
		return true;
	}

	bool PhysXWorldBackend::SetAngularVelocity(BodyHandle body, const glm::vec3& velocity, bool autowake)
	{
		if (!IsFiniteVec3(velocity))
		{
			return false;
		}

		BodyRecord* record = bodies.Get(body);
		physx::PxRigidDynamic* dynamic = record && record->Actor ? record->Actor->is<physx::PxRigidDynamic>() : nullptr;
		if (!dynamic)
		{
			return false;
		}

		dynamic->setAngularVelocity(ToPx(velocity), autowake);
		return true;
	}

	bool PhysXWorldBackend::GetLinearVelocity(BodyHandle body, glm::vec3& velocity) const
	{
		const BodyRecord* record = bodies.Get(body);
		const physx::PxRigidDynamic* dynamic = record && record->Actor ? record->Actor->is<physx::PxRigidDynamic>() : nullptr;
		if (!dynamic)
		{
			return false;
		}

		velocity = ToGlm(dynamic->getLinearVelocity());
		return true;
	}

	bool PhysXWorldBackend::GetAngularVelocity(BodyHandle body, glm::vec3& velocity) const
	{
		const BodyRecord* record = bodies.Get(body);
		const physx::PxRigidDynamic* dynamic = record && record->Actor ? record->Actor->is<physx::PxRigidDynamic>() : nullptr;
		if (!dynamic)
		{
			return false;
		}

		velocity = ToGlm(dynamic->getAngularVelocity());
		return true;
	}

	void PhysXWorldBackend::BeginSimulation(float dt)
	{
		if (!scene || simulating || !(dt > 0.0f) || !std::isfinite(dt))
		{
			return;
		}

		FlushPendingDestroy();
		collisionEvents.clear();
		triggerEvents.clear();
		scene->simulate(dt);
		simulating = true;
	}

	bool PhysXWorldBackend::FetchResults(bool block)
	{
		if (!scene || !simulating)
		{
			return true;
		}

		const bool finished = scene->fetchResults(block);
		if (finished)
		{
			simulating = false;
			FlushPendingDestroy();
		}
		return finished;
	}

	bool PhysXWorldBackend::Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& hit, CollisionLayer filter) const
	{
		if (!scene || simulating || !IsFiniteVec3(origin) || !IsValidDirection(direction, maxDistance))
		{
			return false;
		}

		const glm::vec3 unitDirection = glm::normalize(direction);
		LayerQueryFilter queryFilter(filter, physx::PxQueryHitType::eBLOCK);
		const physx::PxQueryFilterData queryData(
			physx::PxQueryFlag::eSTATIC | physx::PxQueryFlag::eDYNAMIC | physx::PxQueryFlag::ePREFILTER);
		physx::PxRaycastBuffer buffer;

		if (!scene->raycast(ToPx(origin), ToPx(unitDirection), maxDistance, buffer,
			physx::PxHitFlag::eDEFAULT, queryData, &queryFilter) || !buffer.hasBlock)
		{
			return false;
		}

		const physx::PxRaycastHit& pxHit = buffer.block;
		hit.Body = ResolveBody(pxHit.actor);
		hit.Shape = ResolveShape(pxHit.shape);
		hit.Position = ToGlm(pxHit.position);
		hit.Normal = ToGlm(pxHit.normal);
		hit.Distance = pxHit.distance;
		hit.UserData = ResolveUserData(hit.Body);
		return hit.Body && hit.Shape;
	}

	bool PhysXWorldBackend::Sweep(const ShapeDesc& shape, const PhysicsPose& pose, const glm::vec3& direction, float maxDistance, SweepHit& hit, CollisionLayer filter) const
	{
		if (!scene || simulating || !IsValidPose(pose) || !IsValidPose(shape.LocalPose) || !IsValidDirection(direction, maxDistance))
		{
			return false;
		}

		const glm::vec3 unitDirection = glm::normalize(direction);
		physx::PxTransform queryPose = ToPx(pose) * ToPx(shape.LocalPose);
		LayerQueryFilter queryFilter(filter, physx::PxQueryHitType::eBLOCK);
		const physx::PxQueryFilterData queryData(
			physx::PxQueryFlag::eSTATIC | physx::PxQueryFlag::eDYNAMIC | physx::PxQueryFlag::ePREFILTER);
		physx::PxSweepBuffer buffer;

		auto executeSweep = [&](const physx::PxGeometry& geometry)
		{
			return scene->sweep(geometry, queryPose, ToPx(unitDirection), maxDistance, buffer,
				physx::PxHitFlag::eDEFAULT, queryData, &queryFilter);
		};

		bool status = false;
		switch (shape.Type)
		{
			case ShapeType::Box:
			{
				const glm::vec3 extents = shape.Box.HalfExtents;
				if (!IsFiniteVec3(extents) || !(extents.x > 0.0f) || !(extents.y > 0.0f) || !(extents.z > 0.0f))
				{
					return false;
				}
				const physx::PxBoxGeometry geometry(extents.x, extents.y, extents.z);
				status = executeSweep(geometry);
				break;
			}
			case ShapeType::Sphere:
			{
				if (!(shape.Sphere.Radius > 0.0f) || !std::isfinite(shape.Sphere.Radius))
				{
					return false;
				}
				const physx::PxSphereGeometry geometry(shape.Sphere.Radius);
				status = executeSweep(geometry);
				break;
			}
			case ShapeType::Capsule:
			{
				if (!(shape.Capsule.Radius > 0.0f) || !(shape.Capsule.HalfHeight >= 0.0f)
					|| !std::isfinite(shape.Capsule.Radius) || !std::isfinite(shape.Capsule.HalfHeight))
				{
					return false;
				}
				const physx::PxQuat yUp(physx::PxHalfPi, physx::PxVec3(0.0f, 0.0f, 1.0f));
				queryPose.q = queryPose.q * yUp;
				const physx::PxCapsuleGeometry geometry(shape.Capsule.Radius, shape.Capsule.HalfHeight);
				status = executeSweep(geometry);
				break;
			}
			case ShapeType::ConvexMesh:
			case ShapeType::TriangleMesh:
			{
				return false;
			}
		}

		if (!status || !buffer.hasBlock)
		{
			return false;
		}

		const physx::PxSweepHit& pxHit = buffer.block;
		hit.Body = ResolveBody(pxHit.actor);
		hit.Shape = ResolveShape(pxHit.shape);
		hit.Position = ToGlm(pxHit.position);
		hit.Normal = ToGlm(pxHit.normal);
		hit.Distance = pxHit.distance;
		hit.UserData = ResolveUserData(hit.Body);
		return hit.Body && hit.Shape;
	}

	std::size_t PhysXWorldBackend::Overlap(const ShapeDesc& shape, const PhysicsPose& pose, std::span<OverlapHit> hits, CollisionLayer filter) const
	{
		if (!scene || simulating || hits.empty() || !IsValidPose(pose) || !IsValidPose(shape.LocalPose))
		{
			return 0;
		}

		physx::PxTransform queryPose = ToPx(pose) * ToPx(shape.LocalPose);
		const std::size_t maxPhysXHits = static_cast<std::size_t>(std::numeric_limits<physx::PxU32>::max());
		const std::size_t requestedHitCount = std::min(hits.size(), maxPhysXHits);
		std::vector<physx::PxOverlapHit> pxHits(requestedHitCount);
		physx::PxOverlapBuffer buffer(pxHits.data(), static_cast<physx::PxU32>(pxHits.size()));
		LayerQueryFilter queryFilter(filter, physx::PxQueryHitType::eTOUCH);
		const physx::PxQueryFilterData queryData(
			physx::PxQueryFlag::eSTATIC | physx::PxQueryFlag::eDYNAMIC
			| physx::PxQueryFlag::ePREFILTER | physx::PxQueryFlag::eNO_BLOCK);

		auto executeOverlap = [&](const physx::PxGeometry& geometry)
		{
			return scene->overlap(geometry, queryPose, buffer, queryData, &queryFilter);
		};

		bool status = false;
		switch (shape.Type)
		{
			case ShapeType::Box:
			{
				const glm::vec3 extents = shape.Box.HalfExtents;
				if (!IsFiniteVec3(extents) || !(extents.x > 0.0f) || !(extents.y > 0.0f) || !(extents.z > 0.0f))
				{
					return 0;
				}
				const physx::PxBoxGeometry geometry(extents.x, extents.y, extents.z);
				status = executeOverlap(geometry);
				break;
			}
			case ShapeType::Sphere:
			{
				if (!(shape.Sphere.Radius > 0.0f) || !std::isfinite(shape.Sphere.Radius))
				{
					return 0;
				}
				const physx::PxSphereGeometry geometry(shape.Sphere.Radius);
				status = executeOverlap(geometry);
				break;
			}
			case ShapeType::Capsule:
			{
				if (!(shape.Capsule.Radius > 0.0f) || !(shape.Capsule.HalfHeight >= 0.0f)
					|| !std::isfinite(shape.Capsule.Radius) || !std::isfinite(shape.Capsule.HalfHeight))
				{
					return 0;
				}
				const physx::PxQuat yUp(physx::PxHalfPi, physx::PxVec3(0.0f, 0.0f, 1.0f));
				queryPose.q = queryPose.q * yUp;
				const physx::PxCapsuleGeometry geometry(shape.Capsule.Radius, shape.Capsule.HalfHeight);
				status = executeOverlap(geometry);
				break;
			}
			case ShapeType::ConvexMesh:
			case ShapeType::TriangleMesh:
			{
				return 0;
			}
		}

		if (!status)
		{
			return 0;
		}

		const std::size_t count = std::min<std::size_t>(buffer.getNbAnyHits(), hits.size());
		for (std::size_t i = 0; i < count; ++i)
		{
			const physx::PxOverlapHit& pxHit = buffer.getAnyHit(static_cast<physx::PxU32>(i));
			hits[i].Body = ResolveBody(pxHit.actor);
			hits[i].Shape = ResolveShape(pxHit.shape);
			hits[i].UserData = ResolveUserData(hits[i].Body);
		}
		return count;
	}

	physx::PxVec3 PhysXWorldBackend::ToPx(const glm::vec3& value)
	{
		return physx::PxVec3(value.x, value.y, value.z);
	}

	glm::vec3 PhysXWorldBackend::ToGlm(const physx::PxVec3& value)
	{
		return glm::vec3(value.x, value.y, value.z);
	}

	physx::PxQuat PhysXWorldBackend::ToPx(const glm::quat& value)
	{
		const glm::quat normalized = glm::normalize(value);
		return physx::PxQuat(normalized.x, normalized.y, normalized.z, normalized.w);
	}

	glm::quat PhysXWorldBackend::ToGlm(const physx::PxQuat& value)
	{
		return glm::quat(value.w, value.x, value.y, value.z);
	}

	physx::PxTransform PhysXWorldBackend::ToPx(const PhysicsPose& pose)
	{
		return physx::PxTransform(ToPx(pose.Position), ToPx(pose.Rotation));
	}

	PhysicsPose PhysXWorldBackend::ToEngine(const physx::PxTransform& pose)
	{
		return PhysicsPose{ ToGlm(pose.p), ToGlm(pose.q) };
	}

	physx::PxForceMode::Enum PhysXWorldBackend::ToPx(ForceMode mode)
	{
		switch (mode)
		{
			case ForceMode::Force: return physx::PxForceMode::eFORCE;
			case ForceMode::Impulse: return physx::PxForceMode::eIMPULSE;
			case ForceMode::VelocityChange: return physx::PxForceMode::eVELOCITY_CHANGE;
			case ForceMode::Acceleration: return physx::PxForceMode::eACCELERATION;
		}
		return physx::PxForceMode::eFORCE;
	}

	BodyHandle PhysXWorldBackend::ResolveBody(const physx::PxActor* actor) const
	{
		if (!actor)
		{
			return {};
		}

		const physx::PxRigidActor* rigidActor = actor->is<physx::PxRigidActor>();
		if (!rigidActor)
		{
			return {};
		}

		const auto it = actorHandles.find(rigidActor);
		return it != actorHandles.end() ? it->second : BodyHandle{};
	}

	ShapeHandle PhysXWorldBackend::ResolveShape(const physx::PxShape* shape) const
	{
		const auto it = shapeHandles.find(shape);
		return it != shapeHandles.end() ? it->second : ShapeHandle{};
	}

	std::uint64_t PhysXWorldBackend::ResolveUserData(BodyHandle body) const
	{
		const BodyRecord* record = bodies.Get(body);
		return record ? record->UserData : 0;
	}

	void PhysXWorldBackend::FlushPendingDestroy()
	{
		if (simulating || pendingDestroy.empty())
		{
			return;
		}

		for (physx::PxRigidActor* actor : pendingDestroy)
		{
			ReleaseActor(actor);
		}
		pendingDestroy.clear();
	}

	void PhysXWorldBackend::ReleaseActor(physx::PxRigidActor* actor)
	{
		if (!actor)
		{
			return;
		}

		if (physx::PxScene* owner = actor->getScene())
		{
			owner->removeActor(*actor);
		}
		actor->release();
	}

} // namespace Engine
