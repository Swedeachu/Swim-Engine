#pragma once

#include "IPhysicsBackend.h"

#include <memory>
#include <span>

namespace Engine
{

	class PhysicsWorld
	{

	public:

		explicit PhysicsWorld(std::unique_ptr<IPhysicsWorldBackend> backend);
		~PhysicsWorld() = default;

		PhysicsWorld(const PhysicsWorld&) = delete;
		PhysicsWorld& operator=(const PhysicsWorld&) = delete;
		PhysicsWorld(PhysicsWorld&&) noexcept = default;
		PhysicsWorld& operator=(PhysicsWorld&&) noexcept = default;

		bool IsValid() const { return backend != nullptr; }

		PhysicsMaterialHandle CreateMaterial(const PhysicsMaterialDesc& desc);
		void DestroyMaterial(PhysicsMaterialHandle material);
		bool IsMaterialValid(PhysicsMaterialHandle material) const;

		ShapeHandle CreateShape(const ShapeDesc& desc, PhysicsMaterialHandle material);
		void DestroyShape(ShapeHandle shape);
		bool IsShapeValid(ShapeHandle shape) const;

		BodyHandle CreateBody(const BodyDesc& desc);
		void DestroyBody(BodyHandle body);
		bool IsBodyValid(BodyHandle body) const;

		bool SetBodyPose(BodyHandle body, const PhysicsPose& pose, bool autowake = true);
		bool SetKinematicTarget(BodyHandle body, const PhysicsPose& pose);
		bool GetBodyPose(BodyHandle body, PhysicsPose& pose) const;

		bool AddForce(BodyHandle body, const glm::vec3& force, ForceMode mode = ForceMode::Force, bool autowake = true);
		bool SetLinearVelocity(BodyHandle body, const glm::vec3& velocity, bool autowake = true);
		bool SetAngularVelocity(BodyHandle body, const glm::vec3& velocity, bool autowake = true);
		bool GetLinearVelocity(BodyHandle body, glm::vec3& velocity) const;
		bool GetAngularVelocity(BodyHandle body, glm::vec3& velocity) const;

		bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& hit, CollisionLayer filter = {}) const;
		bool Sweep(const ShapeDesc& shape, const PhysicsPose& pose, const glm::vec3& direction, float maxDistance, SweepHit& hit, CollisionLayer filter = {}) const;
		std::size_t Overlap(const ShapeDesc& shape, const PhysicsPose& pose, std::span<OverlapHit> hits, CollisionLayer filter = {}) const;

		std::span<const CollisionEvent> GetCollisionEvents() const;
		std::span<const TriggerEvent> GetTriggerEvents() const;

		void BeginSimulation(float dt);
		bool FetchResults(bool block = true);
		bool IsSimulationInFlight() const;

	private:

		std::unique_ptr<IPhysicsWorldBackend> backend;

	};

} // namespace Engine
