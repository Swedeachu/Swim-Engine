#pragma once

#include "IPhysicsBackend.h"
#include "PhysicsBuoyancy.h"

#include <memory>
#include <span>
#include <vector>

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

		// Force or Impulse at a world point (an off-centre impulse throws with spin).
		bool AddForceAtPosition(BodyHandle body, const glm::vec3& force, const glm::vec3& position, ForceMode mode = ForceMode::Force,
			bool autowake = true);

		bool SetLinearVelocity(BodyHandle body, const glm::vec3& velocity, bool autowake = true);

		bool SetAngularVelocity(BodyHandle body, const glm::vec3& velocity, bool autowake = true);

		bool GetLinearVelocity(BodyHandle body, glm::vec3& velocity) const;

		bool GetAngularVelocity(BodyHandle body, glm::vec3& velocity) const;

		bool GetMass(BodyHandle body, float& mass) const;

		bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& hit, CollisionLayer filter = {}) const;

		bool Sweep(const ShapeDesc& shape, const PhysicsPose& pose, const glm::vec3& direction, float maxDistance, SweepHit& hit, CollisionLayer filter = {}) const;

		std::size_t Overlap(const ShapeDesc& shape, const PhysicsPose& pose, std::span<OverlapHit> hits, CollisionLayer filter = {}) const;

		std::span<const CollisionEvent> GetCollisionEvents() const;

		std::span<const TriggerEvent> GetTriggerEvents() const;

		void BeginSimulation(float dt);

		bool FetchResults(bool block = true);

		bool IsSimulationInFlight() const;

		// Seconds simulated so far (the sum of BeginSimulation steps); fluid surfaces get it.
		double GetSimulationTime() const { return simulationTime; }

		// Buoyancy, applied by BeginSimulation with the same forces on every backend: each
		// buoyant dynamic body in a fluid volume is pushed up by the weight of the fluid it
		// displaces (at the submerged sample points, so it also rights itself) and dragged
		// toward the fluid's flow, linearly and angularly, in proportion to how deep it is.
		// A body uses the first volume (in creation order) whose bounds hold its centre.
		FluidVolumeHandle AddFluidVolume(FluidVolumeDesc desc);

		bool UpdateFluidVolume(FluidVolumeHandle volume, FluidVolumeDesc desc);

		void RemoveFluidVolume(FluidVolumeHandle volume);

		// Replaces any earlier desc of the body. False for an invalid body or desc.
		bool SetBuoyancy(BodyHandle body, const BuoyancyDesc& desc);

		void ClearBuoyancy(BodyHandle body);

		// Null for a body without buoyancy.
		const BuoyancyState* GetBuoyancyState(BodyHandle body) const;

	private:

		void ApplyBuoyancy(float dt);

		void KeepFloatersUp();

		struct FluidVolume
		{
			FluidVolumeHandle Handle;
			FluidVolumeDesc Desc;
		};

		struct BuoyantBody
		{
			BodyHandle Body;
			BuoyancyDesc Desc;
			BuoyancyState State;
		};

		std::unique_ptr<IPhysicsWorldBackend> backend;
		std::vector<FluidVolume> fluids;
		std::vector<BuoyantBody> buoyant;
		std::uint32_t nextFluid = 1;
		double simulationTime = 0.0;

	};

} // namespace Engine
