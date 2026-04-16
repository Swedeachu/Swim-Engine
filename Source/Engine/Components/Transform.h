#pragma once

#include <vector>
#include <cstdint>
#include <algorithm>

#include "Library/glm/glm.hpp"
#include "Library/glm/gtc/quaternion.hpp"
#include "Library/EnTT/entt.hpp" 

namespace Engine
{

	// Forward declare
	class Scene;
	class PhysicsWorld;

	enum class TransformSpace : int
	{
		World, // 0
		Screen, // 1
		Ambiguous
	};

	class Transform
	{

		// Scene is responsible to wire parenting and invalidate subtrees efficiently
		friend class Scene;
		// Physics world will do sync between its actors and their transforms
		friend class PhysicsWorld;

	private:

		glm::vec3 position{ 0.0f, 0.0f, 0.0f };
		glm::vec3 scale{ 1.0f, 1.0f, 1.0f };
		glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f }; // Identity

		// Local dirty + cache
		mutable bool dirty = true;
		mutable glm::mat4 modelMatrix{ 1.0f }; // LOCAL matrix (TRS)

		// World cache + dirty
		mutable bool worldDirty = true;
		mutable glm::mat4 worldMatrix{ 1.0f }; // WORLD matrix
		uint64_t worldVersion = 1; // increments whenever this transforms world answer changes

		inline static bool TransformsDirty = false; // frame flag for stuff like BVH to rebuild
		inline static std::vector<entt::entity> DirtyEntities{}; // coarse dirty list used by scene systems for incremental updates
		inline static uint64_t DirtyEpoch = 1;
		uint64_t lastQueuedDirtyEpoch = 0;
		TransformSpace space = TransformSpace::World;

		// Agnostic layer seperated from specifc rendering clip space for helping with UI layer priority logic such as mouse input.
		// This is a complete hack.
		float readableLayer{ 0 };

		// Parent + children (entity handles)
		entt::entity owner = entt::null;
		entt::entity parent = entt::null;
		std::vector<entt::entity> children; // Scene manages membership

		// --- Physics interpolation state (world-space targets from PhysX) ---

		bool physicsHasTarget = false;

		glm::vec3 physicsPrevWorldPos{ 0.0f, 0.0f, 0.0f };
		glm::vec3 physicsTargetWorldPos{ 0.0f, 0.0f, 0.0f };

		glm::quat physicsPrevWorldRot{ 1.0f, 0.0f, 0.0f, 0.0f };
		glm::quat physicsTargetWorldRot{ 1.0f, 0.0f, 0.0f, 0.0f };

		// Helpers to mark dirty
		void MarkDirty();

		// Called by Scene to invalidate world cache (and optionally local if needed)
		void MarkWorldDirtyOnly();

		void MarkChildrenDirty();
		void QueueDirtyEntity();

		// Helper: parent world rotation (TR only, no scale)
		static glm::quat GetParentWorldRotationTR(const Transform& tf, const entt::registry& registry);

	public:

		Transform() = default;

		Transform
		(
			const glm::vec3& pos,
			const glm::vec3& scl,
			const glm::quat& rot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), // must be identity
			TransformSpace ts = TransformSpace::World
		)
			: position(pos),
			scale(scl),
			rotation(rot),
			dirty(true),
			modelMatrix(1.0f),
			worldDirty(true),
			worldMatrix(1.0f),
			space(ts)
		{}

		const glm::vec3& GetPosition() const { return position; }
		const glm::vec3& GetScale()    const { return scale; }
		const glm::quat& GetRotation() const { return rotation; }
		const bool IsDirty()           const { return dirty; }
		const bool IsWorldDirty()			 const { return worldDirty; }
		uint64_t GetWorldVersion() const { return worldVersion; }

		static bool AreAnyTransformsDirty() { return TransformsDirty; }
		static void ClearGlobalDirtyFlag() { TransformsDirty = false; }
		static void ClearDirtyEntities()
		{
			DirtyEntities.clear();
			++DirtyEpoch;
			if (DirtyEpoch == 0)
			{
				DirtyEpoch = 1;
			}
		}
		static const std::vector<entt::entity>& GetDirtyEntities() { return DirtyEntities; }

		static void MarkEntityDirty(entt::entity entity)
		{
			if (entity != entt::null)
			{
				DirtyEntities.push_back(entity);
				TransformsDirty = true;
			}
		}

		entt::entity GetOwner() const { return owner; }

		void SetPosition(const glm::vec3& pos)
		{
			if (position != pos) { position = pos; MarkDirty(); }
		}

		void SetScale(const glm::vec3& scl)
		{
			if (scale != scl) { scale = scl; MarkDirty(); }
		}

		void SetRotation(const glm::quat& rot);

		void SetRotationEuler(float pitch, float yaw, float roll)
		{
			glm::quat newRotation = glm::quat(glm::vec3(glm::radians(pitch), glm::radians(yaw), glm::radians(roll)));
			SetRotation(newRotation);
		}

		glm::vec3 GetRotationEuler() const
		{
			return glm::degrees(glm::eulerAngles(rotation));
		}

		glm::vec3& GetPositionRef() { MarkDirty(); return position; }
		glm::vec3& GetScaleRef() { MarkDirty(); return scale; }
		glm::quat& GetRotationRef() { MarkDirty(); return rotation; }

		TransformSpace GetTransformSpace() const { return space; }

		void SetTransformSpace(TransformSpace ts)
		{
			if (space != ts) { space = ts; MarkDirty(); }
		}

		// NOTE: Obviously this is only meaningful for screen-space transforms (TransformSpace::Screen).
		// Maps an integer layer to a stable Z in [0,1] for orthographic depth sorting.
		// Higher layer => rendered on top (smaller Z with standard Vulkan depth: LESS, near=0, far=1).
		// kMaxLayers=4096 is a conservative choice that avoids precision issues while providing ample layers.
		void SetScreenSpaceLayer(int layer);

		// NOTE: Only meaningful for screen-space transforms (TransformSpace::Screen).
		// Adjusts this transform's Z value slightly above or below its parent's Z layer.
		// Does nothing if there is no valid parent.
		void SetScreenSpaceLayerRelativeToParent(bool aboveParent);

		// LOCAL 
		const glm::mat4& GetModelMatrix() const;

		glm::quat GetWorldRotation(const entt::registry& registry) const;

		glm::vec3 GetWorldScale(const entt::registry& registry) const;

		// You pretty much always want to call this method, which is kind of scuffed it needs the registry to get passed,
		// very annoying and weird from a gameplay programmer's perspecitve. Same goes for scale and rotation.
		glm::vec3 GetWorldPosition(const entt::registry& registry) const;

		// WORLD (needs registry to walk parent chain)
		// if parent is invalid or missing Transform, treated as no parent.
		const glm::mat4& GetWorldMatrix(const entt::registry& registry) const;

		static glm::mat4 MakeModelTR(const Transform& tf);

		// Maybe we should consider making this a method + caching model tr field and having a dirty flag for this.
		static glm::mat4 MakeWorldTR(const Transform& tf, const entt::registry& registry);

		// World -> Local: Position 
		glm::vec3 WorldToLocalPosition(const entt::registry& registry, const glm::vec3& worldPos) const;

		// World -> Local: Scale 
		// Assumes TRS (no shear). WorldScale = ParentWorldScale * LocalScale (component-wise),
		// so LocalScale = WorldScale / ParentWorldScale.
		glm::vec3 WorldToLocalScale(const entt::registry& registry, const glm::vec3& worldScale) const;

		// World -> Local: Rotation (quat) 
		glm::quat WorldToLocalRotation(const entt::registry& registry, const glm::quat& worldRot) const;

		// World -> Local: Rotation (Euler degrees) 
		glm::quat WorldToLocalRotation(const entt::registry& registry, float pitchDeg, float yawDeg, float rollDeg) const;

		void SetWorldPosition(const entt::registry& registry, const glm::vec3& worldPos);

		void SetWorldScale(const entt::registry& registry, const glm::vec3& worldScale);

		void SetWorldRotation(const entt::registry& registry, const glm::quat& worldRot);

		void SetWorldRotationEuler(const entt::registry& registry, float pitchDeg, float yawDeg, float rollDeg);

		bool HasParent() const { return parent != entt::null; }
		entt::entity GetParent() const { return parent; }

		// --- Physics interpolation API (used by PhysicsWorld) ---

		// Called once per fixed tick after PhysX finished to record the new target.
		void SetPhysicsTargetWorldPose(const entt::registry& registry, const glm::vec3& worldPos, const glm::quat& worldRot);

		// Called each frame by PhysicsWorld to smoothly render between fixed ticks.
		// alpha is in [0,1] where 0 = prev tick pose, 1 = current tick pose.
		void ApplyPhysicsInterpolation(const entt::registry& registry, float alpha);

		void SnapPhysicsToTarget(const entt::registry& registry)
		{
			ApplyPhysicsInterpolation(registry, 1.0f);
		}

		bool HasPhysicsTarget() const { return physicsHasTarget; }

	};

} // Namespace Engine
