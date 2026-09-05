#include "Engine/Systems/Physics/Backends/PhysX/Internal/PhysXPhysicsUtils.h"

#include "Engine/Systems/Physics/Backends/PhysX/PhysXWorldBackend.h"

#include <cmath>

namespace Engine::PhysXPhysicsDetail
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
				| physx::PxPairFlag::eNOTIFY_TOUCH_LOST
				| physx::PxPairFlag::eNOTIFY_CONTACT_POINTS;

			// eNOTIFY_TOUCH_PERSISTS makes PhysX report and extract contacts for
			// every touching pair on every step. Only ask for it when the world
			// actually exposes persisted events.
			const bool reportPersisted = constantBlock != nullptr
				&& constantBlockSize >= sizeof(PhysXFilterShaderConstants)
				&& static_cast<const PhysXFilterShaderConstants*>(constantBlock)->ReportPersistedContacts != 0u;
			if (reportPersisted)
			{
				pairFlags |= physx::PxPairFlag::eNOTIFY_TOUCH_PERSISTS;
			}

			return physx::PxFilterFlag::eDEFAULT;
		}

} // namespace Engine::PhysXPhysicsDetail
