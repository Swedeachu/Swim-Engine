#include "PhysicsWorld.h"

#include <utility>

namespace Engine
{

	PhysicsWorld::PhysicsWorld(std::unique_ptr<IPhysicsWorldBackend> selectedBackend)
		: backend(std::move(selectedBackend))
	{}

	PhysicsMaterialHandle PhysicsWorld::CreateMaterial(const PhysicsMaterialDesc& desc)
	{
		return backend ? backend->CreateMaterial(desc) : PhysicsMaterialHandle{};
	}

	void PhysicsWorld::DestroyMaterial(PhysicsMaterialHandle material)
	{
		if (backend)
		{
			backend->DestroyMaterial(material);
		}
	}

	bool PhysicsWorld::IsMaterialValid(PhysicsMaterialHandle material) const
	{
		return backend && backend->IsMaterialValid(material);
	}

	ShapeHandle PhysicsWorld::CreateShape(const ShapeDesc& desc, PhysicsMaterialHandle material)
	{
		return backend ? backend->CreateShape(desc, material) : ShapeHandle{};
	}

	void PhysicsWorld::DestroyShape(ShapeHandle shape)
	{
		if (backend)
		{
			backend->DestroyShape(shape);
		}
	}

	bool PhysicsWorld::IsShapeValid(ShapeHandle shape) const
	{
		return backend && backend->IsShapeValid(shape);
	}

	BodyHandle PhysicsWorld::CreateBody(const BodyDesc& desc)
	{
		return backend ? backend->CreateBody(desc) : BodyHandle{};
	}

	void PhysicsWorld::DestroyBody(BodyHandle body)
	{
		if (backend)
		{
			backend->DestroyBody(body);
		}
	}

	bool PhysicsWorld::IsBodyValid(BodyHandle body) const
	{
		return backend && backend->IsBodyValid(body);
	}

	bool PhysicsWorld::SetBodyPose(BodyHandle body, const PhysicsPose& pose, bool autowake)
	{
		return backend && backend->SetBodyPose(body, pose, autowake);
	}

	bool PhysicsWorld::SetKinematicTarget(BodyHandle body, const PhysicsPose& pose)
	{
		return backend && backend->SetKinematicTarget(body, pose);
	}

	bool PhysicsWorld::GetBodyPose(BodyHandle body, PhysicsPose& pose) const
	{
		return backend && backend->GetBodyPose(body, pose);
	}

	bool PhysicsWorld::AddForce(BodyHandle body, const glm::vec3& force, ForceMode mode, bool autowake)
	{
		return backend && backend->AddForce(body, force, mode, autowake);
	}

	bool PhysicsWorld::SetLinearVelocity(BodyHandle body, const glm::vec3& velocity, bool autowake)
	{
		return backend && backend->SetLinearVelocity(body, velocity, autowake);
	}

	bool PhysicsWorld::SetAngularVelocity(BodyHandle body, const glm::vec3& velocity, bool autowake)
	{
		return backend && backend->SetAngularVelocity(body, velocity, autowake);
	}

	bool PhysicsWorld::GetLinearVelocity(BodyHandle body, glm::vec3& velocity) const
	{
		return backend && backend->GetLinearVelocity(body, velocity);
	}

	bool PhysicsWorld::GetAngularVelocity(BodyHandle body, glm::vec3& velocity) const
	{
		return backend && backend->GetAngularVelocity(body, velocity);
	}

	bool PhysicsWorld::Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& hit, CollisionLayer filter) const
	{
		return backend && backend->Raycast(origin, direction, maxDistance, hit, filter);
	}

	bool PhysicsWorld::Sweep(const ShapeDesc& shape, const PhysicsPose& pose, const glm::vec3& direction, float maxDistance, SweepHit& hit, CollisionLayer filter) const
	{
		return backend && backend->Sweep(shape, pose, direction, maxDistance, hit, filter);
	}

	std::size_t PhysicsWorld::Overlap(const ShapeDesc& shape, const PhysicsPose& pose, std::span<OverlapHit> hits, CollisionLayer filter) const
	{
		return backend ? backend->Overlap(shape, pose, hits, filter) : 0;
	}

	std::span<const CollisionEvent> PhysicsWorld::GetCollisionEvents() const
	{
		return backend ? backend->GetCollisionEvents() : std::span<const CollisionEvent>{};
	}

	std::span<const TriggerEvent> PhysicsWorld::GetTriggerEvents() const
	{
		return backend ? backend->GetTriggerEvents() : std::span<const TriggerEvent>{};
	}

	void PhysicsWorld::BeginSimulation(float dt)
	{
		if (backend)
		{
			backend->BeginSimulation(dt);
		}
	}

	bool PhysicsWorld::FetchResults(bool block)
	{
		return !backend || backend->FetchResults(block);
	}

	bool PhysicsWorld::IsSimulationInFlight() const
	{
		return backend && backend->IsSimulationInFlight();
	}

} // namespace Engine
