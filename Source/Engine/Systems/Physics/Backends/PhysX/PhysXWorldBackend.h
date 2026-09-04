#pragma once

#include "Engine/Systems/Physics/IPhysicsBackend.h"
#include "Engine/Systems/Physics/Internal/GenerationalHandleTable.h"

#include "PxPhysicsAPI.h"
#include "extensions/PxRigidBodyExt.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Engine
{

	class PhysXWorldBackend final : public IPhysicsWorldBackend
	{

	public:

		PhysXWorldBackend(physx::PxPhysics& physics, physx::PxCpuDispatcher& dispatcher, PhysicsWorldDesc desc);
		~PhysXWorldBackend() override;

		bool Initialize();

		PhysicsMaterialHandle CreateMaterial(const PhysicsMaterialDesc& desc) override;
		void DestroyMaterial(PhysicsMaterialHandle material) override;
		bool IsMaterialValid(PhysicsMaterialHandle material) const override;

		ShapeHandle CreateShape(const ShapeDesc& desc, PhysicsMaterialHandle material) override;
		void DestroyShape(ShapeHandle shape) override;
		bool IsShapeValid(ShapeHandle shape) const override;

		BodyHandle CreateBody(const BodyDesc& desc) override;
		void DestroyBody(BodyHandle body) override;
		bool IsBodyValid(BodyHandle body) const override;

		bool SetBodyPose(BodyHandle body, const PhysicsPose& pose, bool autowake = true) override;
		bool SetKinematicTarget(BodyHandle body, const PhysicsPose& pose) override;
		bool GetBodyPose(BodyHandle body, PhysicsPose& pose) const override;

		bool AddForce(BodyHandle body, const glm::vec3& force, ForceMode mode = ForceMode::Force, bool autowake = true) override;
		bool SetLinearVelocity(BodyHandle body, const glm::vec3& velocity, bool autowake = true) override;
		bool SetAngularVelocity(BodyHandle body, const glm::vec3& velocity, bool autowake = true) override;
		bool GetLinearVelocity(BodyHandle body, glm::vec3& velocity) const override;
		bool GetAngularVelocity(BodyHandle body, glm::vec3& velocity) const override;

		void BeginSimulation(float dt) override;
		bool FetchResults(bool block = true) override;
		bool IsSimulationInFlight() const override { return simulating; }

		bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& hit, CollisionLayer filter = {}) const override;
		bool Sweep(const ShapeDesc& shape, const PhysicsPose& pose, const glm::vec3& direction, float maxDistance, SweepHit& hit, CollisionLayer filter = {}) const override;
		std::size_t Overlap(const ShapeDesc& shape, const PhysicsPose& pose, std::span<OverlapHit> hits, CollisionLayer filter = {}) const override;

		std::span<const CollisionEvent> GetCollisionEvents() const override { return collisionEvents; }
		std::span<const TriggerEvent> GetTriggerEvents() const override { return triggerEvents; }

	private:

		class SimulationEventCallback;

		struct PxReleaser
		{
			void operator()(physx::PxScene* ptr) const
			{
				if (ptr)
				{
					ptr->release();
				}
			}
		};

		struct BodyRecord
		{
			physx::PxRigidActor* Actor = nullptr;
			ShapeHandle Shape{};
			std::uint64_t UserData = 0;
		};

		physx::PxPhysics& physics;
		physx::PxCpuDispatcher& dispatcher;
		PhysicsWorldDesc worldDesc{};
		std::unique_ptr<physx::PxScene, PxReleaser> scene;

		GenerationalHandleTable<PhysicsMaterialHandle, physx::PxMaterial*> materials;
		GenerationalHandleTable<ShapeHandle, physx::PxShape*> shapes;
		GenerationalHandleTable<BodyHandle, BodyRecord> bodies;
		std::unordered_map<const physx::PxRigidActor*, BodyHandle> actorHandles;
		std::unordered_map<const physx::PxShape*, ShapeHandle> shapeHandles;
		std::vector<physx::PxRigidActor*> pendingDestroy;
		std::vector<CollisionEvent> collisionEvents;
		std::vector<TriggerEvent> triggerEvents;
		std::unique_ptr<SimulationEventCallback> eventCallback;
		bool simulating = false;

		static physx::PxVec3 ToPx(const glm::vec3& value);
		static glm::vec3 ToGlm(const physx::PxVec3& value);
		static physx::PxQuat ToPx(const glm::quat& value);
		static glm::quat ToGlm(const physx::PxQuat& value);
		static physx::PxTransform ToPx(const PhysicsPose& pose);
		static PhysicsPose ToEngine(const physx::PxTransform& pose);
		static physx::PxForceMode::Enum ToPx(ForceMode mode);

		BodyHandle ResolveBody(const physx::PxActor* actor) const;
		ShapeHandle ResolveShape(const physx::PxShape* shape) const;
		std::uint64_t ResolveUserData(BodyHandle body) const;

		void FlushPendingDestroy();
		void ReleaseActor(physx::PxRigidActor* actor);

	};

}
