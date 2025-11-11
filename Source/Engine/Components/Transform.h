#pragma once

#include "Library/glm/glm.hpp"
#include "Library/glm/gtc/quaternion.hpp"
#include "Library/EnTT/entt.hpp" 

namespace Engine
{

	enum class TransformSpace
	{
		World, // 0
		Screen, // 1
		Ambiguous
	};

	class Transform
	{

		// Scene is responsible to wire parenting and invalidate subtrees efficiently
		friend class Scene;

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

		inline static bool TransformsDirty = false; // frame flag for stuff like BVH to rebuild
		TransformSpace space = TransformSpace::World;

		// Agnostic layer seperated from specifc rendering clip space for helping with UI layer priority logic such as mouse input.
		// This is a complete hack.
		float readableLayer{ 0 }; 

		// Parent + children (entity handles)
		entt::entity parent = entt::null;
		std::vector<entt::entity> children; // Scene manages membership

		// Helpers to mark dirty
		void MarkDirty()
		{
			dirty = true;
			worldDirty = true;
			TransformsDirty = true;
			MarkChildrenDirty();
		}

		// Called by Scene to invalidate world cache (and optionally local if needed)
		void MarkWorldDirtyOnly()
		{
			worldDirty = true;
			TransformsDirty = true;
			MarkChildrenDirty();
		}

		void MarkChildrenDirty()
		{
			// scuffed hack, but is the most painless for the rest of the engine
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

	public:

		Transform() = default;

		Transform
		(
			const glm::vec3& pos,
			const glm::vec3& scl,
			const glm::quat& rot = glm::quat(),
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

		static bool AreAnyTransformsDirty() { return TransformsDirty; }
		static void ClearGlobalDirtyFlag() { TransformsDirty = false; }

		void SetPosition(const glm::vec3& pos)
		{
			if (position != pos) { position = pos; MarkDirty(); }
		}

		void SetScale(const glm::vec3& scl)
		{
			if (scale != scl) { scale = scl; MarkDirty(); }
		}

		void SetRotation(const glm::quat& rot)
		{
			if (rotation != rot) { rotation = rot; MarkDirty(); }
		}

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
		void SetScreenSpaceLayer(int layer)
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

		// NOTE: Only meaningful for screen-space transforms (TransformSpace::Screen).
		// Adjusts this transform's Z value slightly above or below its parent's Z layer.
		// Does nothing if there is no valid parent.
		void SetScreenSpaceLayerRelativeToParent(bool aboveParent)
		{
			if (parent == entt::null) return;

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

		// LOCAL 
		const glm::mat4& GetModelMatrix() const
		{
			if (dirty)
			{
				modelMatrix =
					glm::translate(glm::mat4(1.0f), position)
					* glm::mat4_cast(rotation)
					* glm::scale(glm::mat4(1.0f), scale);
				dirty = false;
			}
			return modelMatrix;
		}

		glm::vec3 GetWorldScale(const entt::registry& registry) const
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

		// You pretty much always want to call this method, which is kind of scuffed it needs the registry to get passed,
		// very annoying and weird from a gameplay programmer's perspecitve. Same goes for scale and rotation.
		glm::vec3 GetWorldPosition(const entt::registry& registry) const
		{
			const glm::mat4& world = GetWorldMatrix(registry);
			return glm::vec3(world[3]);
		}

		// WORLD (needs registry to walk parent chain)
		// if parent is invalid or missing Transform, treated as no parent.
		const glm::mat4& GetWorldMatrix(const entt::registry& registry) const
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

		static glm::mat4 MakeModelTR(const Transform& tf)
		{
			glm::mat4 T = glm::translate(glm::mat4(1.0f), tf.GetPosition());
			glm::mat4 R = glm::mat4_cast(tf.GetRotation());
			return T * R;
		}

		// Maybe we should consider making this a method + caching model tr field and having a dirty flag for this.
		static glm::mat4 MakeWorldTR(const Transform& tf, const entt::registry& registry)
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

		// World -> Local: Position 
		glm::vec3 WorldToLocalPosition(const entt::registry& registry, const glm::vec3& worldPos) const
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

		// World -> Local: Scale 
		// Assumes TRS (no shear). WorldScale = ParentWorldScale * LocalScale (component-wise),
		// so LocalScale = WorldScale / ParentWorldScale.
		glm::vec3 WorldToLocalScale(const entt::registry& registry, const glm::vec3& worldScale) const
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

		// Helper: parent world rotation (TR only, no scale)
		static inline glm::quat GetParentWorldRotationTR(const Transform& tf, const entt::registry& registry)
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

		// World -> Local: Rotation (quat) 
		glm::quat WorldToLocalRotation(const entt::registry& registry, const glm::quat& worldRot) const
		{
			const glm::quat parentWorldRot = GetParentWorldRotationTR(*this, registry);
			// local = inv(parentRot) * world
			glm::quat localRot = glm::normalize(glm::conjugate(parentWorldRot) * worldRot);
			return localRot;
		}

		// World -> Local: Rotation (Euler degrees) 
		glm::quat WorldToLocalRotation(const entt::registry& registry, float pitchDeg, float yawDeg, float rollDeg) const
		{
			const glm::vec3 eulerRad = glm::radians(glm::vec3(pitchDeg, yawDeg, rollDeg));
			const glm::quat worldRot = glm::quat(eulerRad);
			return WorldToLocalRotation(registry, worldRot);
		}

		void SetWorldPosition(const entt::registry& registry, const glm::vec3& worldPos)
		{
			const glm::vec3 localPos = WorldToLocalPosition(registry, worldPos);
			SetPosition(localPos);
		}

		void SetWorldScale(const entt::registry& registry, const glm::vec3& worldScale)
		{
			const glm::vec3 localScale = WorldToLocalScale(registry, worldScale);
			SetScale(localScale);
		}

		void SetWorldRotation(const entt::registry& registry, const glm::quat& worldRot)
		{
			const glm::quat localRot = WorldToLocalRotation(registry, worldRot);
			SetRotation(localRot);
		}

		void SetWorldRotationEuler(const entt::registry& registry, float pitchDeg, float yawDeg, float rollDeg)
		{
			const glm::quat localRot = WorldToLocalRotation(registry, pitchDeg, yawDeg, rollDeg);
			SetRotation(localRot);
		}

		bool HasParent() const { return parent != entt::null; }
		entt::entity GetParent() const { return parent; }

	};

}
