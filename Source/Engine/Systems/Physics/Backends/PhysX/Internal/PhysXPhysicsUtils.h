#pragma once

// Small stateless helpers shared by the PhysX backend's query filter and
// PhysXWorldBackend's own methods, plus the PxSimulationFilterShader used to
// configure every PhysX scene this backend creates. Split out of the
// anonymous namespace that used to sit at the top of the former monolithic
// PhysXWorldBackend.cpp.

#include "Engine/Systems/Physics/PhysicsTypes.h"

#include "PxPhysicsAPI.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Engine::PhysXPhysicsDetail
{

	bool IsFiniteVec3(const glm::vec3& value);
	bool IsFiniteQuat(const glm::quat& value);
	bool IsValidPose(const PhysicsPose& pose);
	bool IsValidDirection(const glm::vec3& direction, float maxDistance);
	bool LayersMatch(const CollisionLayer& query, const physx::PxFilterData& object);

	physx::PxFilterFlags SwimSimulationFilterShader(
		physx::PxFilterObjectAttributes attributes0,
		physx::PxFilterData filterData0,
		physx::PxFilterObjectAttributes attributes1,
		physx::PxFilterData filterData1,
		physx::PxPairFlags& pairFlags,
		const void* constantBlock,
		physx::PxU32 constantBlockSize);

} // namespace Engine::PhysXPhysicsDetail
