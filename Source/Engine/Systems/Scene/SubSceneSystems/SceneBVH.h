#pragma once

#include <cstdint>
#include <utility>
#include <vector>
#include <unordered_map>
#include <limits>

#include "Library/glm/glm.hpp"
#include "Library/EnTT/entt.hpp"
#include "SceneDebugDraw.h"
#include "Engine/Systems/Renderer/Core/MathTypes/MathAlgorithms.h"

namespace Engine
{

	class Transform;
	class Mesh;
	enum class AABBFrustumClassification : uint8_t;
	struct Frustum;

	class SceneBVH
	{

	public:

		struct GpuWideSnapshotNode
		{
			glm::vec4 minX{ 0.0f };
			glm::vec4 minY{ 0.0f };
			glm::vec4 minZ{ 0.0f };
			glm::vec4 maxX{ 0.0f };
			glm::vec4 maxY{ 0.0f };
			glm::vec4 maxZ{ 0.0f };
			int childRef[4]{ 0, 0, 0, 0 };
			uint32_t childCount = 0;
			uint32_t childTraversalOrder[4]{ 0, 1, 2, 3 };
		};

		struct GpuWideSnapshotLeaf
		{
			entt::entity entity{ entt::null };
		};

		explicit SceneBVH(entt::registry& registry);

		void Init();
		void Update();
		void DebugRender();
		void UpdateIfNeeded(entt::observer& frustumObserver);
		void QueryFrustum(const Frustum& frustum, std::vector<entt::entity>& outVisible) const;
		void QueryFrustumParallel(const Frustum& frustum, std::vector<entt::entity>& outVisible) const;

		void BuildGpuWideSnapshot(
			std::vector<GpuWideSnapshotNode>& outNodes,
			std::vector<GpuWideSnapshotLeaf>& outLeaves,
			uint32_t* outRootIndex = nullptr,
			uint32_t* outMaxDepth = nullptr
		) const;

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
			if (wideRoot == -1)
			{
				return;
			}

			WideTraversalItem stack[WideTraversalStackMax];
			int stackSize = 0;

			if (!PushWideRootIfVisible(frustum, stack, stackSize))
			{
				return;
			}

			while (stackSize > 0)
			{
				const WideTraversalItem item = stack[--stackSize];
				const WideNode& node = wideNodes[item.wideIndex];

				if (item.fullyInside)
				{
					for (int orderIndex = static_cast<int>(node.childCount) - 1; orderIndex >= 0; --orderIndex)
					{
						const uint8_t childIndex = node.childTraversalOrder[orderIndex];
						if (childIndex >= node.childCount)
						{
							continue;
						}

						const int childRef = node.childRef[childIndex];
						if (childRef == InvalidWideChild)
						{
							continue;
						}

						if (IsEncodedWideLeaf(childRef))
						{
							const int leafIndex = DecodeWideLeaf(childRef);
							const entt::entity entity = nodes[leafIndex].entity;
							if (entity != entt::null)
							{
								callback(entity);
							}
						}
						else if (stackSize < WideTraversalStackMax)
						{
							stack[stackSize++] = { childRef, true };
						}
					}
					continue;
				}

				uint8_t fullyInsideMask = 0;
				const uint8_t visibleMask = GetWideNodeVisibleMask(node, frustum, &fullyInsideMask);

				uint8_t traversalOrder[4]{ 0, 1, 2, 3 };
				uint8_t traversalCount = 0;
				CollectWideTraversalOrder(node, visibleMask, fullyInsideMask, traversalOrder, traversalCount);

				for (int orderIndex = static_cast<int>(traversalCount) - 1; orderIndex >= 0; --orderIndex)
				{
					const uint8_t childIndex = traversalOrder[orderIndex];
					const uint8_t bit = static_cast<uint8_t>(1u << childIndex);
					const int childRef = node.childRef[childIndex];
					if (childRef == InvalidWideChild)
					{
						continue;
					}

					if (IsEncodedWideLeaf(childRef))
					{
						const int leafIndex = DecodeWideLeaf(childRef);
						const entt::entity entity = nodes[leafIndex].entity;
						if (entity != entt::null)
						{
							callback(entity);
						}
					}
					else if (stackSize < WideTraversalStackMax)
					{
						stack[stackSize++] = { childRef, (fullyInsideMask & bit) != 0 };
					}
				}
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
			mutable uint8_t lastClassification = 0;
			mutable bool lastVisible = false;
			mutable bool hasCullHistory = false;

			[[nodiscard]] bool IsLeaf() const
			{
				return left == -1 && right == -1;
			}
		};

		static constexpr int InvalidWideChild = INT32_MIN;

		struct alignas(16) WideNode
		{
			alignas(16) float minX[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
			alignas(16) float minY[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
			alignas(16) float minZ[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
			alignas(16) float maxX[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
			alignas(16) float maxY[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
			alignas(16) float maxZ[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
			int childRef[4]{ InvalidWideChild, InvalidWideChild, InvalidWideChild, InvalidWideChild };
			int parent = -1;
			uint8_t parentSlot = 0xFF;
			uint8_t childCount = 0;
			mutable glm::vec3 lastCullAABBMin{ 0.0f, 0.0f, 0.0f };
			mutable glm::vec3 lastCullAABBMax{ 0.0f, 0.0f, 0.0f };
			mutable uint64_t lastFrustumRevision = 0;
			mutable uint8_t lastRejectedPlane = 0;
			mutable uint8_t lastClassification = 0;
			mutable uint8_t lastVisibleMask = 0;
			mutable uint8_t lastFullyInsideMask = 0;
			mutable uint8_t childTraversalOrder[4]{ 0, 1, 2, 3 };
			mutable bool lastVisible = false;
			mutable bool hasCullHistory = false;
			AABB traversalAABB;
		};

		static bool IsEncodedWideLeaf(int childRef)
		{
			return childRef < 0 && childRef != InvalidWideChild;
		}

		static int EncodeWideLeaf(int leafIndex)
		{
			return -(leafIndex + 1);
		}

		static int DecodeWideLeaf(int childRef)
		{
			return -childRef - 1;
		}

		struct WideTraversalItem
		{
			int wideIndex;
			bool fullyInside;
		};

		static constexpr int WideTraversalStackMax = 1024;

		struct ParallelVisibleScratch
		{
			std::vector<entt::entity> visible;
		};

		void EnsureParallelQueryScratch(size_t workerSlots, size_t seedItemHint) const;

		bool IsAABBVisible(const Frustum& frustum, const AABB& aabb) const;
		AABBFrustumClassification ClassifyNode(const BVHNode& node, const Frustum& frustum, const AABB& aabb) const;
		AABBFrustumClassification ClassifyWideNode(const WideNode& node, const Frustum& frustum) const;
		uint8_t GetWideNodeVisibleMask(const WideNode& node, const Frustum& frustum, uint8_t* outFullyInsideMask) const;
		void CollectWideTraversalOrder(const WideNode& node, uint8_t visibleMask, uint8_t fullyInsideMask, uint8_t* outOrder, uint8_t& outCount) const;
		bool PushWideRootIfVisible(const Frustum& frustum, WideTraversalItem* stack, int& stackSize) const;
		void TraverseWideSubtree(int wideIndex, bool fullyInside, const Frustum& frustum, std::vector<entt::entity>& outVisible) const;
		const AABB& GetTraversalAABB(const BVHNode& node) const;
		AABB CalculateWorldAABB(const std::shared_ptr<Mesh>& mesh, const Transform& transform);
		AABB CalculateWorldAABB(entt::entity entity, const glm::vec3& localMin, const glm::vec3& localMax, const Transform& transform);

		int BuildRecursive(std::vector<int>& leafIndices, int begin, int end);
		void RefitBinaryAncestors(int leafIndex);
		void FullRebuild();
		int BuildWideRecursive(int binaryNodeIndex, int parentWideIndex, uint8_t parentSlot);
		void BuildWideHierarchy();
		void SetWideChildBounds(int wideIndex, uint8_t childSlot, const AABB& aabb);
		void UpdateWideNodeBoundsFromChildren(int wideIndex);
		void RefitWideAncestorsFromLeaf(int leafIndex);
		inline void PushIfVisible(int nodeIndex, const Frustum& frustum, bool parentFullyInside, std::vector<std::pair<int, bool>>& stack) const;

		entt::registry& registry;
		entt::observer topologyObserver;

		std::vector<BVHNode> nodes; // binary BVH kept for updates and ray casting
		std::vector<WideNode> wideNodes; // 4-wide SoA frustum traversal layout
		std::vector<int> leafToWideParent;
		std::vector<uint8_t> leafToWideSlot;
		std::unordered_map<entt::entity, int> entityToLeaf; // entity leaf index
		int root = -1;
		int wideRoot = -1;

		SceneDebugDraw* debugDrawer = nullptr;

		mutable std::vector<WideTraversalItem> parallelSeedItemsScratch;
		mutable std::vector<entt::entity> parallelDirectVisibleScratch;
		mutable std::vector<ParallelVisibleScratch> parallelVisibleScratch;

		bool forceUpdate = false;
	};

}
