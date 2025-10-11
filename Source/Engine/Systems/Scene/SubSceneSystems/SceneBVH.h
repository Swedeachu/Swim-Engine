#pragma once

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
		void RemoveEntity(entt::entity entity);

		bool ShouldForceUpdate() const { return forceUpdate; }

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

			TraverseFrustumCallback(root, frustum, callback);
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
			if (!RayIntersectsAABB(ray, nodes[root].aabb, tMin, tMax, tRoot))
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
				const bool hitL = RayIntersectsAABB(ray, nodes[node.left].aabb, tMin, tMax, tL);
				const bool hitR = RayIntersectsAABB(ray, nodes[node.right].aabb, tMin, tMax, tR);

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
			AABB aabb;             // Bounds that encloses children OR the entity
			int  left = -1;        // index of left child or -1 if leaf
			int  right = -1;       // index of right child or -1 if leaf
			entt::entity entity{ entt::null }; // valid only for leaves

			[[nodiscard]] bool IsLeaf() const
			{
				return left == -1 && right == -1;
			}
		};

		bool IsAABBVisible(const Frustum& frustum, const AABB& aabb) const;
		AABB CalculateWorldAABB(const std::shared_ptr<Mesh>& mesh, const Transform& transform);

		int BuildRecursive(std::vector<int>& leafIndices, int begin, int end);
		void Refit(int nodeIndex);
		void FullRebuild();
		inline void PushIfVisible(int nodeIndex, const Frustum& frustum, std::vector<int>& stack) const;

		entt::registry& registry;
		entt::observer observer;

		std::vector<BVHNode> nodes; // breadth first array
		std::unordered_map<entt::entity, int> entityToLeaf; // entity leaf index
		int root = -1;

		SceneDebugDraw* debugDrawer = nullptr;

		bool forceUpdate = false;

		template<typename Func>
		void TraverseFrustumCallback(int nodeIdx, const Frustum& frustum, Func&& callback) const
		{
			if (nodeIdx >= nodes.size())
			{
				std::cout << "Node index does not exist in BVH: " << std::to_string(nodeIdx) << std::endl;
				return;
			}

			const BVHNode& node = nodes[nodeIdx];

			// Cull test for internal and leaf nodes
			if (!IsAABBVisible(frustum, node.aabb))
			{
				return; // skip entire branch
			}

			if (node.IsLeaf())
			{
				callback(node.entity); // visible leaf so trigger callback
			}
			else
			{
				// Traverse children
				TraverseFrustumCallback(node.left, frustum, callback);
				TraverseFrustumCallback(node.right, frustum, callback);
			}
		}

	};

}
