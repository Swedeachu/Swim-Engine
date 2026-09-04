#pragma once

#include "PhysicsTypes.h"

#include <memory>
#include <span>

namespace Engine
{

	class IPhysicsWorldBackend
	{
	public:

		virtual ~IPhysicsWorldBackend() = default;

		virtual PhysicsMaterialHandle CreateMaterial(const PhysicsMaterialDesc& desc) = 0;
		virtual void DestroyMaterial(PhysicsMaterialHandle material) = 0;
		virtual bool IsMaterialValid(PhysicsMaterialHandle material) const = 0;

		virtual ShapeHandle CreateShape(const ShapeDesc& desc, PhysicsMaterialHandle material) = 0;
		virtual void DestroyShape(ShapeHandle shape) = 0;
		virtual bool IsShapeValid(ShapeHandle shape) const = 0;

		virtual BodyHandle CreateBody(const BodyDesc& desc) = 0;
		virtual void DestroyBody(BodyHandle body) = 0;
		virtual bool IsBodyValid(BodyHandle body) const = 0;

		virtual bool SetBodyPose(BodyHandle body, const PhysicsPose& pose, bool autowake = true) = 0;
		virtual bool SetKinematicTarget(BodyHandle body, const PhysicsPose& pose) = 0;
		virtual bool GetBodyPose(BodyHandle body, PhysicsPose& pose) const = 0;

		virtual bool AddForce(BodyHandle body, const glm::vec3& force, ForceMode mode = ForceMode::Force, bool autowake = true) = 0;
		virtual bool SetLinearVelocity(BodyHandle body, const glm::vec3& velocity, bool autowake = true) = 0;
		virtual bool SetAngularVelocity(BodyHandle body, const glm::vec3& velocity, bool autowake = true) = 0;
		virtual bool GetLinearVelocity(BodyHandle body, glm::vec3& velocity) const = 0;
		virtual bool GetAngularVelocity(BodyHandle body, glm::vec3& velocity) const = 0;

		virtual void BeginSimulation(float dt) = 0;
		virtual bool FetchResults(bool block = true) = 0;
		virtual bool IsSimulationInFlight() const = 0;

		virtual bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& hit, CollisionLayer filter = {}) const = 0;
		virtual bool Sweep(const ShapeDesc& shape, const PhysicsPose& pose, const glm::vec3& direction, float maxDistance, SweepHit& hit, CollisionLayer filter = {}) const = 0;
		virtual std::size_t Overlap(const ShapeDesc& shape, const PhysicsPose& pose, std::span<OverlapHit> hits, CollisionLayer filter = {}) const = 0;

		virtual std::span<const CollisionEvent> GetCollisionEvents() const = 0;
		virtual std::span<const TriggerEvent> GetTriggerEvents() const = 0;

	};

	class IPhysicsBackend
	{
	public:

		virtual ~IPhysicsBackend() = default;

		virtual bool Initialize(unsigned int workerThreads) = 0;
		virtual void Shutdown() = 0;
		virtual std::unique_ptr<IPhysicsWorldBackend> CreateWorld(const PhysicsWorldDesc& desc) = 0;
		virtual const char* GetName() const = 0;
	};

}
