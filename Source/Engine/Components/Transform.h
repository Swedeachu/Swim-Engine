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

		// Parent + children (entity handles)
		entt::entity parent = entt::null;
		std::vector<entt::entity> children; // Scene manages membership

		// Helpers to mark dirty
		void MarkDirty()
		{
			dirty = true;
			worldDirty = true; // local change implies world change
			TransformsDirty = true;
		}

		// Called by Scene to invalidate world cache (and optionally local if needed)
		void MarkWorldDirtyOnly()
		{
			worldDirty = true;
			TransformsDirty = true;
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
				const auto& pTf = registry.get<Transform>(parent);
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
			const auto& parentTf = registry.get<Transform>(tf.parent);
			return MakeWorldTR(parentTf, registry) * localTR;
		}

		bool HasParent() const { return parent != entt::null; }
		entt::entity GetParent() const { return parent; }

	};

}
