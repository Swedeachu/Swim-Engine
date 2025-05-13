#pragma once

#include "Library/glm/glm.hpp"
#include "Library/EnTT/entt.hpp"
#include "SceneDebugDraw.h"

namespace Engine
{

	class Transform;
	class Mesh;
	struct Frustum;

	class SceneBVH
	{

	public:

		struct AABB
		{
			glm::vec3 min{ std::numeric_limits<float>::max() };
			glm::vec3 max{ -std::numeric_limits<float>::max() };
		};

		explicit SceneBVH(entt::registry& registry);

		void Init();
		void Update();
		void DebugRender();
		void UpdateIfNeeded(entt::observer& frustumObserver);
		void QueryFrustum(const Frustum& frustum, std::vector<entt::entity>& outVisible) const;
		void RemoveEntity(entt::entity entity);

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
