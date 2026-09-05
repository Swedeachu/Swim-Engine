#include "Engine/Systems/Physics/Backends/Jolt/Internal/JoltPhysicsUtils.h"

#include <cmath>

namespace Engine::JoltPhysicsDetail
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

		bool LayersMatch(const CollisionLayer& a, const CollisionLayer& b)
		{
			return (a.Layer & b.Mask) != 0u && (b.Layer & a.Mask) != 0u;
		}

		JPH::EMotionType ToJoltMotionType(MotionType motion)
		{
			switch (motion)
			{
				case MotionType::Static:
					return JPH::EMotionType::Static;
				case MotionType::Dynamic:
					return JPH::EMotionType::Dynamic;
				case MotionType::Kinematic:
					return JPH::EMotionType::Kinematic;
			}
			return JPH::EMotionType::Static;
		}

} // namespace Engine::JoltPhysicsDetail
