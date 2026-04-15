#include "PCH.h"
#include "Transform.h"
#include "Engine/Systems/Scene/Scene.h"

namespace Engine
{

	void Transform::MarkDirty()
	{
		dirty = true;
		worldDirty = true;
		++worldVersion;
		TransformsDirty = true;
		MarkChildrenDirty();
	}

	void Transform::MarkWorldDirtyOnly()
	{
		worldDirty = true;
		++worldVersion;
		TransformsDirty = true;
		MarkChildrenDirty();
	}

	void Transform::MarkChildrenDirty()
	{
		// scuffed hack, but is the most painless for the rest of the engine. This is dangerous because what if this transform is not in the active scene?
		auto scene = SwimEngine::GetInstance()->GetSceneSystem()->GetActiveScene();
		if (!scene) return;

		entt::registry& reg = scene->GetRegistry();

		for (entt::entity child : children)
		{
			if (reg.valid(child))
			{
				if (reg.any_of<Transform>(child))
				{
					Transform& tf = reg.get<Transform>(child);
					tf.MarkDirty();
				}
			}
		}
	}

	static bool IsFiniteFloat(float f)
	{
		return std::isfinite(f);
	}

	static bool IsFiniteQuat(const glm::quat& q)
	{
		if (!IsFiniteFloat(q.w)) { return false; }
		if (!IsFiniteFloat(q.x)) { return false; }
		if (!IsFiniteFloat(q.y)) { return false; }
		if (!IsFiniteFloat(q.z)) { return false; }
		return true;
	}

	static glm::quat SafeNormalizeQuat(const glm::quat& q)
	{
		if (!IsFiniteQuat(q))
		{
			return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		}

		const float len2 = glm::dot(q, q);
		if (!std::isfinite(len2) || len2 <= 0.0f)
		{
			return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		}

		return glm::normalize(q);
	}

	void Transform::SetPhysicsTargetWorldPose(const entt::registry& registry, const glm::vec3& worldPos, const glm::quat& worldRot)
	{
		const glm::quat safeRot = SafeNormalizeQuat(worldRot);

		if (physicsHasTarget)
		{
			physicsPrevWorldPos = physicsTargetWorldPos;
			physicsPrevWorldRot = physicsTargetWorldRot;
		}
		else
		{
			// First time: avoid popping by snapping immediately.
			physicsPrevWorldPos = worldPos;
			physicsPrevWorldRot = safeRot;

			SetWorldPosition(registry, worldPos);
			SetWorldRotation(registry, safeRot);
		}

		physicsTargetWorldPos = worldPos;
		physicsTargetWorldRot = safeRot;
		physicsHasTarget = true;
	}

	void Transform::ApplyPhysicsInterpolation(const entt::registry& registry, float alpha)
	{
		if (!physicsHasTarget)
		{
			return;
		}

		float t = alpha;

		if (t < 0.0f) { t = 0.0f; }
		if (t > 1.0f) { t = 1.0f; }

		const glm::vec3 worldPos = glm::mix(physicsPrevWorldPos, physicsTargetWorldPos, t);

		glm::quat worldRot = glm::slerp(physicsPrevWorldRot, physicsTargetWorldRot, t);
		worldRot = SafeNormalizeQuat(worldRot);

		SetWorldPosition(registry, worldPos);
		SetWorldRotation(registry, worldRot);
	}

	void Transform::SetScreenSpaceLayerRelativeToParent(bool aboveParent)
	{
		if (parent == entt::null) return;

		// This is dangerous because what if this transform is not in the active scene?
		auto scene = SwimEngine::GetInstance()->GetSceneSystem()->GetActiveScene();
		if (!scene) return;

		entt::registry& reg = scene->GetRegistry();
		if (!reg.valid(parent) || !reg.any_of<Transform>(parent)) return;

		Transform& pTf = reg.get<Transform>(parent);
		// const float parentZ = pTf.GetPosition().z;
		const float parentZ = pTf.readableLayer;

		constexpr float kOffset = 1e-5f;  // tiny bias to avoid z-fighting
		float z = position.z;

		// Set readable layer agnostic to render context (HACK)
		{
			if (aboveParent)
			{
				z = glm::max(parentZ - kOffset, -1.0f);
			}
			else
			{
				z = glm::min(parentZ + kOffset, +1.0f);
			}
			readableLayer = z;
		}

		if constexpr (SwimEngine::CONTEXT == SwimEngine::RenderContext::Vulkan)
		{
			// Vulkan: [0,1], smaller z is in front
			if (aboveParent)
			{
				z = glm::max(parentZ - kOffset, 0.0f);
			}
			else
			{
				z = glm::min(parentZ + kOffset, 1.0f);
			}
		}
		else if constexpr (SwimEngine::CONTEXT == SwimEngine::RenderContext::OpenGL)
		{
			// Default OpenGL: [-1,1], smaller (more negative) is in front
			if (aboveParent)
			{
				z = glm::max(parentZ - kOffset, -1.0f);
			}
			else
			{
				z = glm::min(parentZ + kOffset, +1.0f);
			}
		}

		if (z != position.z)
		{
			GetPositionRef().z = z;
		}
	}

	void Transform::SetScreenSpaceLayer(int layer)
	{
		constexpr int kMaxLayers = 4096;
		constexpr float kEpsilon = 1e-6f;

		// Clamp layer
		int L = layer;

		if (L < 0)
		{
			L = 0;
		}

		if (L >= kMaxLayers)
		{
			L = kMaxLayers - 1;
		}

		float z = position.z;

		// Set readable layer agnostic to render context (HACK)
		{
			float zed = readableLayer;
			const float stepNdc = 2.0f / (kMaxLayers + 2);
			zed = +1.0f - (static_cast<float>(L) + 1.0f) * stepNdc;
			zed = glm::clamp(z, -1.0f + kEpsilon, +1.0f - kEpsilon);
			readableLayer = zed;
		}

		if constexpr (SwimEngine::CONTEXT == SwimEngine::RenderContext::Vulkan)
		{
			// Spread evenly in (0,1) with a margin at both ends.
			const float step = 1.0f / (kMaxLayers + 2); // margin = one step at each end
			z = 1.0f - (static_cast<float>(L) + 1.0f) * step; // higher L -> smaller z (front)
			// clamp for safety
			z = glm::clamp(z, 0.0f + kEpsilon, 1.0f - kEpsilon);
		}
		else if constexpr (SwimEngine::CONTEXT == SwimEngine::RenderContext::OpenGL)
		{
			// Default OpenGL NDC is [-1, 1] with near = -1 (front), far = +1 (back).
			// Reserve margins at both ends and distribute kMaxLayers steps across (-1, +1).
			const float stepNdc = 2.0f / (kMaxLayers + 2);      // total span 2.0
			// Start near +1 (back) and move toward -1 (front) as L increases.
			z = +1.0f - (static_cast<float>(L) + 1.0f) * stepNdc;
			// Tiny guard to avoid living exactly at the clip planes
			z = glm::clamp(z, -1.0f + kEpsilon, +1.0f - kEpsilon);
		}

		if (z != position.z)
		{
			GetPositionRef().z = z;
		}
	}

	const glm::mat4& Transform::GetModelMatrix() const
	{
		if (dirty)
		{
			// Ensure we never build matrices from a garbage quat.
			// (rotation is normally normalized in SetRotation/ctor, but this also covers GetRotationRef() abuse.)
			const glm::quat safeRot = SafeNormalizeQuat(rotation);

			modelMatrix =
				glm::translate(glm::mat4(1.0f), position)
				* glm::mat4_cast(safeRot)
				* glm::scale(glm::mat4(1.0f), scale);

			dirty = false;
		}

		return modelMatrix;
	}

	glm::quat Transform::GetWorldRotation(const entt::registry& registry) const
	{
		// If no parent, world rotation is just local rotation (but safe)
		if (parent == entt::null || !registry.valid(parent) || !registry.any_of<Transform>(parent))
		{
			return SafeNormalizeQuat(rotation);
		}

		// Recursively get parent's world rotation and combine with local
		const Transform& pTf = registry.get<Transform>(parent);
		const glm::quat parentWorldRot = pTf.GetWorldRotation(registry);

		return SafeNormalizeQuat(parentWorldRot * rotation);
	}

	glm::vec3 Transform::GetWorldScale(const entt::registry& registry) const
	{
		const glm::mat4& world = GetWorldMatrix(registry);

		const glm::vec3 col0 = glm::vec3(world[0]);
		const glm::vec3 col1 = glm::vec3(world[1]);
		const glm::vec3 col2 = glm::vec3(world[2]);

		const glm::vec3 worldScale(
			glm::length(col0),
			glm::length(col1),
			glm::length(col2)
		);

		return worldScale;
	}

	glm::vec3 Transform::GetWorldPosition(const entt::registry& registry) const
	{
		const glm::mat4& world = GetWorldMatrix(registry);
		return glm::vec3(world[3]);
	}

	const glm::mat4& Transform::GetWorldMatrix(const entt::registry& registry) const
	{
		if (!worldDirty)
		{
			return worldMatrix;
		}

		// compute local first 
		const glm::mat4& local = GetModelMatrix();

		if (parent != entt::null && registry.valid(parent) && registry.any_of<Transform>(parent))
		{
			const Transform& pTf = registry.get<Transform>(parent);
			worldMatrix = pTf.GetWorldMatrix(registry) * local; // recursion w/ caching
		}
		else
		{
			worldMatrix = local;
		}

		worldDirty = false;

		return worldMatrix;
	}

	glm::mat4 Transform::MakeModelTR(const Transform& tf)
	{
		glm::mat4 T = glm::translate(glm::mat4(1.0f), tf.GetPosition());
		glm::mat4 R = glm::mat4_cast(tf.GetRotation());
		return T * R;
	}

	glm::mat4 Transform::MakeWorldTR(const Transform& tf, const entt::registry& registry)
	{
		// Start with the local TR (no scale)
		glm::mat4 localTR = MakeModelTR(tf);

		// If there's no valid parent, this is the world TR
		if (tf.parent == entt::null || !registry.valid(tf.parent) || !registry.any_of<Transform>(tf.parent))
		{
			return localTR;
		}

		// Recursively multiply by parent's world TR
		const Transform& parentTf = registry.get<Transform>(tf.parent);
		return MakeWorldTR(parentTf, registry) * localTR;
	}

	glm::vec3 Transform::WorldToLocalPosition(const entt::registry& registry, const glm::vec3& worldPos) const
	{
		if (parent == entt::null || !registry.valid(parent) || !registry.any_of<Transform>(parent))
		{
			return worldPos;
		}

		const Transform& pTf = registry.get<Transform>(parent);
		const glm::mat4& parentWorld = pTf.GetWorldMatrix(registry);
		const glm::mat4 invParentWorld = glm::inverse(parentWorld);

		const glm::vec4 local = invParentWorld * glm::vec4(worldPos, 1.0f);
		return glm::vec3(local);
	}

	glm::vec3 Transform::WorldToLocalScale(const entt::registry& registry, const glm::vec3& worldScale) const
	{
		if (parent == entt::null || !registry.valid(parent) || !registry.any_of<Transform>(parent))
		{
			return worldScale;
		}

		const Transform& pTf = registry.get<Transform>(parent);
		const glm::vec3 pWorldScale = pTf.GetWorldScale(registry);

		glm::vec3 localScale = worldScale;
		const float eps = 1e-6f;

		if (std::abs(pWorldScale.x) > eps) { localScale.x /= pWorldScale.x; }
		if (std::abs(pWorldScale.y) > eps) { localScale.y /= pWorldScale.y; }
		if (std::abs(pWorldScale.z) > eps) { localScale.z /= pWorldScale.z; }

		return localScale;
	}

	glm::quat Transform::GetParentWorldRotationTR(const Transform& tf, const entt::registry& registry)
	{
		if (tf.parent == entt::null || !registry.valid(tf.parent) || !registry.any_of<Transform>(tf.parent))
		{
			return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		}

		const Transform& pTf = registry.get<Transform>(tf.parent);
		const glm::mat4 parentTR = Transform::MakeWorldTR(pTf, registry);
		const glm::mat3 parentR = glm::mat3(parentTR);
		return glm::quat_cast(parentR);
	}

	glm::quat Transform::WorldToLocalRotation(const entt::registry& registry, const glm::quat& worldRot) const
	{
		const glm::quat parentWorldRot = GetParentWorldRotationTR(*this, registry);
		// local = inv(parentRot) * world
		glm::quat localRot = glm::normalize(glm::conjugate(parentWorldRot) * worldRot);
		return localRot;
	}

	glm::quat Transform::WorldToLocalRotation(const entt::registry& registry, float pitchDeg, float yawDeg, float rollDeg) const
	{
		const glm::vec3 eulerRad = glm::radians(glm::vec3(pitchDeg, yawDeg, rollDeg));
		const glm::quat worldRot = glm::quat(eulerRad);
		return WorldToLocalRotation(registry, worldRot);
	}

	void Transform::SetWorldPosition(const entt::registry& registry, const glm::vec3& worldPos)
	{
		const glm::vec3 localPos = WorldToLocalPosition(registry, worldPos);
		SetPosition(localPos);
	}

	void Transform::SetWorldScale(const entt::registry& registry, const glm::vec3& worldScale)
	{
		const glm::vec3 localScale = WorldToLocalScale(registry, worldScale);
		SetScale(localScale);
	}

	void Transform::SetWorldRotation(const entt::registry& registry, const glm::quat& worldRot)
	{
		const glm::quat localRot = WorldToLocalRotation(registry, worldRot);
		SetRotation(localRot);
	}

	void Transform::SetWorldRotationEuler(const entt::registry& registry, float pitchDeg, float yawDeg, float rollDeg)
	{
		const glm::quat localRot = WorldToLocalRotation(registry, pitchDeg, yawDeg, rollDeg);
		SetRotation(localRot);
	}

	void Transform::SetRotation(const glm::quat& rot)
	{
		const glm::quat safe = SafeNormalizeQuat(rot);

		if (rotation != safe)
		{
			rotation = safe;
			MarkDirty();
		}
	}

} // Namespace Engine
