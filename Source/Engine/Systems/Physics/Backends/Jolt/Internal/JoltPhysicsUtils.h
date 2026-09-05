#pragma once

// Small stateless helpers shared by the Jolt backend's broadphase/pair
// filters, the contact callback, and JoltWorldBackend's own methods. Split
// out of the anonymous namespace that used to sit at the top of the former
// monolithic JoltWorldBackend.cpp.

#include "Engine/Systems/Physics/PhysicsTypes.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/MotionType.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Engine::JoltPhysicsDetail
{

	namespace BroadPhaseLayers
	{
		static constexpr JPH::BroadPhaseLayer NonMoving(0);
		static constexpr JPH::BroadPhaseLayer Moving(1);
		static constexpr JPH::uint Count = 2;
	}

	bool IsFiniteVec3(const glm::vec3& value);
	bool IsFiniteQuat(const glm::quat& value);
	bool IsValidPose(const PhysicsPose& pose);
	bool IsValidDirection(const glm::vec3& direction, float maxDistance);
	bool LayersMatch(const CollisionLayer& a, const CollisionLayer& b);
	JPH::EMotionType ToJoltMotionType(MotionType motion);

} // namespace Engine::JoltPhysicsDetail
