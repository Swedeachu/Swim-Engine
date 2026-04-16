#pragma once

#include <cstdint>
#include <utility>
#include <vector>
#include <unordered_map>

#include "Library/glm/glm.hpp"
#include "Library/EnTT/entt.hpp"
#include "SceneDebugDraw.h"
#include "Engine/Systems/Renderer/Core/MathTypes/MathAlgorithms.h"

namespace Engine
{

	class Transform;
	class Mesh;
	struct Frustum;

	class SceneBVH
	{

	public:

		explicit SceneBVH(entt::registry& registry);

		void Init();
		void Update();
		void DebugRender();
		void UpdateIfNeeded(entt::observer& frustumObserver);
		void QueryFrustum(const Frustum& frustum, std::vector<entt::entity>& outVisible) const;
		bool IsFullyVisible(const Frustum& frustum) const;
		void RemoveEntity(entt::entity entity);

		bool ShouldForceUpdate() const { return forceUpdate; }
		void ForceUpdateNextFrame() { forceUpdate = true; }

		void SetDebugDrawer(SceneDebugDraw* drawer)
		{
			debugDrawer = drawer;
		}

		template<typename Func>
		void QueryFrustumCallback(const Frustum& frustum, Func&& callback) const
		{
			if (root == -1)
			{
				return;
			}

			std::vector<std::pair<int, bool>> stack;
			stack.reserve(128);
			PushIfVisible(root, frustum, false, stack);

			while (!stack.empty())
			{
				const auto [idx, fullyInside] = stack.back();
				stack.pop_back();

				const BVHNode& node = nodes[idx];
				if (node.IsLeaf())
				{
					if (node.entity != entt::null)
					{
						callback(node.entity);
					}
					continue;
				}

				PushIfVisible(node.left, frustum, fullyInside, stack);
				PushIfVisible(node.right, frustum, fullyInside, stack);
			}
		}

		// outTHit is an optional output parameter that lets you know where along the ray the closest hit occurred.
		entt::entity RayCastClosestHit
		(
			const Ray& ray,
			float tMin = 0.0f,
			float tMax = std::numeric_limits<float>::infinity(),
			float* outTHit = nullptr
		) const;

		// Visits all leaf AABBs hit; callback can early-out
		template<typename Func>
		void RayCastCallback
		(
			const Ray& ray,
			Func&& callback,
			float tMin,
			float tMax
		) const
		{
			if (root == -1) return;

			// Manual stack (index + tnear). No heap, predictable.
			static constexpr int STACK_MAX = 512;
			struct Item { int idx; float tnear; };
			Item stack[STACK_MAX];
			int sp = 0;

			float tRoot;
			if (!RayIntersectsAABB(ray, GetTraversalAABB(nodes[root]), tMin, tMax, tRoot))
			{
				return;
			}

			stack[sp++] = { root, tRoot };

			while (sp)
			{
				// LIFO pop (nearer child is pushed last to pop next)
				const Item it = stack[--sp];
				const int idx = it.idx;
				const auto& node = nodes[idx];

				if (node.IsLeaf())
				{
					if (node.entity == entt::null)
					{
						continue;
					}

					float tLeaf;
					if (RayIntersectsAABB(ray, node.aabb, tMin, tMax, tLeaf))
					{
						// callback returns false to stop early
						if (!callback(node.entity, tLeaf, node.aabb))
						{
							return;
						}
					}
					continue;
				}

				// Internal: test children; push farther first so nearer is popped next
				float tL, tR;
				const bool hitL = RayIntersectsAABB(ray, GetTraversalAABB(nodes[node.left]), tMin, tMax, tL);
				const bool hitR = RayIntersectsAABB(ray, GetTraversalAABB(nodes[node.right]), tMin, tMax, tR);

				if (hitL & hitR) // single branch for both
				{
					// Push farther first, then nearer (stack pop gets nearer next)
					const bool leftIsNear = (tL <= tR);
					const int nearIdx = leftIsNear ? node.left : node.right;
					const int farIdx = leftIsNear ? node.right : node.left;
					const float tNear = leftIsNear ? tL : tR;
					const float tFar = leftIsNear ? tR : tL;

					if (sp + 2 <= STACK_MAX)
					{
						stack[sp++] = { farIdx,  tFar };
						stack[sp++] = { nearIdx, tNear };
					}
				}
				else if (hitL)
				{
					if (sp + 1 <= STACK_MAX) stack[sp++] = { node.left, tL };
				}
				else if (hitR)
				{
					if (sp + 1 <= STACK_MAX) stack[sp++] = { node.right, tR };
				}
			}
		}

	private:

		struct BVHNode
		{
			AABB aabb;             // Tight bounds that encloses children OR the entity
			AABB fatAABB;          // Loose bounds used for traversal coherence and cheaper updates
			int  left = -1;        // index of left child or -1 if leaf
			int  right = -1;       // index of right child or -1 if leaf
			int  parent = -1;      // index of parent or -1 if root
			entt::entity entity{ entt::null }; // valid only for leaves

			mutable glm::vec3 lastCullAABBMin{ 0.0f, 0.0f, 0.0f };
			mutable glm::vec3 lastCullAABBMax{ 0.0f, 0.0f, 0.0f };
			mutable uint64_t lastFrustumRevision = 0;
			mutable uint8_t lastRejectedPlane = 0;
			mutable bool lastVisible = false;
			mutable bool hasCullHistory = false;

			uint64_t lastRefitEpoch = 0;

			[[nodiscard]] bool IsLeaf() const
			{
				return left == -1 && right == -1;
			}
		};

		bool IsAABBVisible(const Frustum& frustum, const AABB& aabb) const;
		bool IsNodeVisible(const BVHNode& node, const Frustum& frustum, const AABB& aabb) const;
		const AABB& GetTraversalAABB(const BVHNode& node) const;
		AABB CalculateWorldAABB(const std::shared_ptr<Mesh>& mesh, const Transform& transform);
		AABB CalculateWorldAABB(entt::entity entity, const glm::vec3& localMin, const glm::vec3& localMax, const Transform& transform);

		int BuildRecursive(std::vector<int>& leafIndices, int begin, int end);
		void Refit(int nodeIndex);
		void RefitAncestors(int leafIndex, uint64_t epoch);
		void FullRebuild();
		inline void PushIfVisible(int nodeIndex, const Frustum& frustum, bool parentFullyInside, std::vector<std::pair<int, bool>>& stack) const;

		entt::registry& registry;
		entt::observer topologyObserver;

		std::vector<BVHNode> nodes; // breadth first array
		std::unordered_map<entt::entity, int> entityToLeaf; // entity leaf index
		int root = -1;
		uint64_t refitEpoch = 1;

		SceneDebugDraw* debugDrawer = nullptr;

		bool forceUpdate = false;
	};

}
