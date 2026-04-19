#include "PCH.h"
#include "SceneBVH.h"
#include "Engine/Components/Material.h"
#include "Engine/Components/CompositeMaterial.h"
#include "Engine/Components/Transform.h"
#include "Engine/Components/Internal/FrustumCullCache.h"
#include "Engine/Systems/Renderer/Core/Meshes/Mesh.h"
#include "Engine/Systems/Renderer/Core/Camera/Frustum.h"
#include "Engine/Utility/ParallelUtils.h"

#ifndef SWIM_BVH_USE_SSE
#define SWIM_BVH_USE_SSE 0
#endif

#ifndef SWIM_BVH_ENABLE_INTRIN
#define SWIM_BVH_ENABLE_INTRIN 1 // 1 to enable
#endif

#if SWIM_BVH_ENABLE_INTRIN && (defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && (_M_IX86_FP >= 2)))
#include <immintrin.h>
#undef SWIM_BVH_USE_SSE
#define SWIM_BVH_USE_SSE 1
#endif

namespace Engine
{

	namespace
	{

		constexpr int kWideNodeArity = 4;
		constexpr int kSAHBins = 8;
		constexpr int kFrustumPlaneCount = 6;

		void BuildPlaneTraversalOrder(uint8_t firstPlane, uint8_t* outOrder)
		{
			const uint8_t clampedFirstPlane = firstPlane < kFrustumPlaneCount ? firstPlane : 0;
			outOrder[0] = clampedFirstPlane;

			uint8_t writeIndex = 1;
			for (uint8_t planeIndex = 0; planeIndex < kFrustumPlaneCount; ++planeIndex)
			{
				if (planeIndex == clampedFirstPlane)
				{
					continue;
				}

				outOrder[writeIndex++] = planeIndex;
			}
		}

		AABB MergeAABBs(const AABB& a, const AABB& b)
		{
			AABB merged;
			merged.min = glm::min(a.min, b.min);
			merged.max = glm::max(a.max, b.max);
			return merged;
		}

		AABB MakeFatAABB(const AABB& aabb)
		{
			const glm::vec3 extents = glm::max(aabb.max - aabb.min, glm::vec3(0.0f));
			const glm::vec3 padding = glm::max(extents * 0.10f, glm::vec3(0.05f));

			AABB fat;
			fat.min = aabb.min - padding;
			fat.max = aabb.max + padding;
			return fat;
		}

		AABB MakeFatAABBMotionAware(const AABB& previousAABB, const AABB& currentAABB)
		{
			AABB fat = MakeFatAABB(currentAABB);

			const glm::vec3 previousCenter = 0.5f * (previousAABB.min + previousAABB.max);
			const glm::vec3 currentCenter = 0.5f * (currentAABB.min + currentAABB.max);
			const glm::vec3 motion = currentCenter - previousCenter;
			const glm::vec3 motionPadding = glm::abs(motion) * 0.50f;

			for (int axis = 0; axis < 3; ++axis)
			{
				if (motion[axis] > 0.0f)
				{
					fat.max[axis] += motionPadding[axis];
				}
				else if (motion[axis] < 0.0f)
				{
					fat.min[axis] -= motionPadding[axis];
				}
			}

			return fat;
		}

		float ComputeSurfaceArea(const AABB& aabb)
		{
			const glm::vec3 size = glm::max(aabb.max - aabb.min, glm::vec3(0.0f));
			return 2.0f * (size.x * size.y + size.x * size.z + size.y * size.z);
		}

		bool HasRenderableSubMaterials(const CompositeMaterial& composite, glm::vec3& outLocalMin, glm::vec3& outLocalMax)
		{
			outLocalMin = glm::vec3(FLT_MAX);
			outLocalMax = glm::vec3(-FLT_MAX);

			bool foundAny = false;
			for (const auto& mat : composite.subMaterials)
			{
				if (!mat || !mat->mesh || !mat->mesh->meshBufferData)
				{
					continue;
				}

				const glm::vec3& min = glm::vec3(mat->mesh->meshBufferData->aabbMin);
				const glm::vec3& max = glm::vec3(mat->mesh->meshBufferData->aabbMax);

				outLocalMin = glm::min(outLocalMin, min);
				outLocalMax = glm::max(outLocalMax, max);
				foundAny = true;
			}

			return foundAny;
		}

		bool IsFiniteAABB(const AABB& aabb)
		{
			return aabb.min.x <= aabb.max.x && aabb.min.y <= aabb.max.y && aabb.min.z <= aabb.max.z;
		}

	}

	SceneBVH::SceneBVH(entt::registry& registry)
		: registry{ registry }
	{}

	void SceneBVH::EnsureParallelQueryScratch(size_t workerSlots, size_t seedItemHint) const
	{
		if (parallelVisibleScratch.size() < workerSlots)
		{
			parallelVisibleScratch.resize(workerSlots);
		}

		const size_t reservePerSlot = std::max<size_t>(seedItemHint * 4, 32);
		for (size_t i = 0; i < workerSlots; ++i)
		{
			parallelVisibleScratch[i].visible.clear();
			if (parallelVisibleScratch[i].visible.capacity() < reservePerSlot)
			{
				parallelVisibleScratch[i].visible.reserve(reservePerSlot);
			}
		}

		parallelSeedItemsScratch.clear();
		if (parallelSeedItemsScratch.capacity() < seedItemHint)
		{
			parallelSeedItemsScratch.reserve(seedItemHint);
		}

		parallelDirectVisibleScratch.clear();
		if (parallelDirectVisibleScratch.capacity() < seedItemHint * 2)
		{
			parallelDirectVisibleScratch.reserve(seedItemHint * 2);
		}
	}

	void SceneBVH::Init()
	{
		topologyObserver.connect(registry, entt::collector
			.group<Transform, Material>()
			.group<Transform, CompositeMaterial>());

		registry.on_destroy<Transform>().connect<&SceneBVH::RemoveEntity>(*this);
		registry.on_destroy<Material>().connect<&SceneBVH::RemoveEntity>(*this);
		registry.on_destroy<CompositeMaterial>().connect<&SceneBVH::RemoveEntity>(*this);
	}

#define EXPAND_CORNER(x, y, z)                                            \
{                                                                         \
    glm::vec3 c = glm::vec3(model * glm::vec4(x, y, z, 1.0f));            \
    worldMin = glm::min(worldMin, c);                                     \
    worldMax = glm::max(worldMax, c);                                     \
}

	AABB SceneBVH::CalculateWorldAABB(const std::shared_ptr<Mesh>& mesh, const Transform& transform)
	{
		return CalculateWorldAABB(entt::null, glm::vec3(mesh->meshBufferData->aabbMin), glm::vec3(mesh->meshBufferData->aabbMax), transform);
	}

	AABB SceneBVH::CalculateWorldAABB(entt::entity entity, const glm::vec3& localMin, const glm::vec3& localMax, const Transform& transform)
	{
		const glm::mat4& model = transform.GetWorldMatrix(registry);

		if (entity != entt::null && registry.any_of<FrustumCullCache>(entity))
		{
			auto& cache = registry.get<FrustumCullCache>(entity);
			cache.Update(localMin, localMax, model, transform.GetWorldVersion());
			return cache.GetWorldAABB();
		}

		return Frustum::BuildWorldAABB(localMin, localMax, model);
	}

	void SceneBVH::Update()
	{
		static constexpr float kRebuildThreshold = 0.20f;

		if (root == -1 || topologyObserver.size() > 0 || forceUpdate)
		{
			FullRebuild();
			forceUpdate = false;
			topologyObserver.clear();
			return;
		}

		const std::vector<entt::entity>& dirtyEntities = Transform::GetDirtyEntities();
		if (dirtyEntities.empty())
		{
			return;
		}

		const float preRootArea = (root != -1) ? ComputeSurfaceArea(nodes[root].fatAABB) : 0.0f;
		bool anyLeafEscapedFatBounds = false;

		for (entt::entity e : dirtyEntities)
		{
			if (e == entt::null || !registry.valid(e) || !registry.any_of<Transform>(e))
			{
				continue;
			}

			const Transform& tf = registry.get<Transform>(e);
			if (tf.GetTransformSpace() != TransformSpace::World)
			{
				if (entityToLeaf.find(e) != entityToLeaf.end())
				{
					RemoveEntity(e);
				}
				continue;
			}

			bool hasRenderableBounds = false;
			AABB worldAABB{};

			if (registry.all_of<Material>(e))
			{
				const std::shared_ptr<MaterialData>& mat = registry.get<Material>(e).data;
				if (mat && mat->mesh && mat->mesh->meshBufferData)
				{
					worldAABB = CalculateWorldAABB(e, glm::vec3(mat->mesh->meshBufferData->aabbMin), glm::vec3(mat->mesh->meshBufferData->aabbMax), tf);
					hasRenderableBounds = true;
				}
			}
			else if (registry.all_of<CompositeMaterial>(e))
			{
				glm::vec3 localMin;
				glm::vec3 localMax;
				const CompositeMaterial& comp = registry.get<CompositeMaterial>(e);
				if (HasRenderableSubMaterials(comp, localMin, localMax))
				{
					worldAABB = CalculateWorldAABB(e, localMin, localMax, tf);
					hasRenderableBounds = true;
				}
			}

			if (!hasRenderableBounds)
			{
				if (entityToLeaf.find(e) != entityToLeaf.end())
				{
					RemoveEntity(e);
				}
				continue;
			}

			auto it = entityToLeaf.find(e);
			if (it == entityToLeaf.end())
			{
				forceUpdate = true;
				continue;
			}

			const int leafIndex = it->second;
			BVHNode& leaf = nodes[leafIndex];
			const AABB previousAABB = leaf.aabb;
			leaf.aabb = worldAABB;
			leaf.hasCullHistory = false;

			if (!AABBInsideAABB(leaf.aabb, leaf.fatAABB))
			{
				leaf.fatAABB = MakeFatAABBMotionAware(previousAABB, leaf.aabb);
				anyLeafEscapedFatBounds = true;
			}

			RefitBinaryAncestors(leafIndex);
			RefitWideAncestorsFromLeaf(leafIndex);
		}

		if (forceUpdate)
		{
			FullRebuild();
			forceUpdate = false;
			topologyObserver.clear();
			return;
		}

		if (anyLeafEscapedFatBounds && root != -1 && preRootArea > 0.0f)
		{
			const float postArea = ComputeSurfaceArea(nodes[root].fatAABB);
			const float expansion = postArea / preRootArea;
			if (expansion > 1.0f + kRebuildThreshold)
			{
				FullRebuild();
				forceUpdate = false;
				topologyObserver.clear();
				return;
			}
		}
	}

	int SceneBVH::BuildRecursive(std::vector<int>& leafIndices, int begin, int end)
	{
		const int count = end - begin;
		if (count == 1)
		{
			return leafIndices[begin];
		}

		AABB centroidBox;
		centroidBox.min = glm::vec3(FLT_MAX);
		centroidBox.max = glm::vec3(-FLT_MAX);

		for (int i = begin; i < end; ++i)
		{
			const AABB& aabb = nodes[leafIndices[i]].aabb;
			const glm::vec3 center = 0.5f * (aabb.min + aabb.max);
			centroidBox.min = glm::min(centroidBox.min, center);
			centroidBox.max = glm::max(centroidBox.max, center);
		}

		glm::vec3 extents = centroidBox.max - centroidBox.min;
		int axis = 0;
		if (extents.y > extents.x && extents.y > extents.z)
		{
			axis = 1;
		}
		else if (extents.z > extents.x)
		{
			axis = 2;
		}

		int mid = begin + count / 2;
		const float axisExtent = extents[axis];

		if (axisExtent > 1e-6f && count > 2)
		{
			struct Bin
			{
				AABB bounds;
				int count = 0;
			};

			Bin bins[kSAHBins];
			for (int i = begin; i < end; ++i)
			{
				const AABB& aabb = nodes[leafIndices[i]].aabb;
				const glm::vec3 center = 0.5f * (aabb.min + aabb.max);
				float t = (center[axis] - centroidBox.min[axis]) / axisExtent;
				t = glm::clamp(t, 0.0f, 0.999999f);
				const int binIndex = glm::clamp(static_cast<int>(t * static_cast<float>(kSAHBins)), 0, kSAHBins - 1);

				bins[binIndex].count++;
				if (!IsFiniteAABB(bins[binIndex].bounds))
				{
					bins[binIndex].bounds = aabb;
				}
				else
				{
					bins[binIndex].bounds = MergeAABBs(bins[binIndex].bounds, aabb);
				}
			}

			AABB leftBounds[kSAHBins - 1];
			AABB rightBounds[kSAHBins - 1];
			int leftCounts[kSAHBins - 1]{};
			int rightCounts[kSAHBins - 1]{};

			AABB runningLeft;
			runningLeft.min = glm::vec3(FLT_MAX);
			runningLeft.max = glm::vec3(-FLT_MAX);
			int runningLeftCount = 0;
			for (int i = 0; i < kSAHBins - 1; ++i)
			{
				if (bins[i].count > 0)
				{
					runningLeft = IsFiniteAABB(runningLeft) ? MergeAABBs(runningLeft, bins[i].bounds) : bins[i].bounds;
					runningLeftCount += bins[i].count;
				}
				leftBounds[i] = runningLeft;
				leftCounts[i] = runningLeftCount;
			}

			AABB runningRight;
			runningRight.min = glm::vec3(FLT_MAX);
			runningRight.max = glm::vec3(-FLT_MAX);
			int runningRightCount = 0;
			for (int i = kSAHBins - 1; i > 0; --i)
			{
				if (bins[i].count > 0)
				{
					runningRight = IsFiniteAABB(runningRight) ? MergeAABBs(runningRight, bins[i].bounds) : bins[i].bounds;
					runningRightCount += bins[i].count;
				}
				rightBounds[i - 1] = runningRight;
				rightCounts[i - 1] = runningRightCount;
			}

			float bestCost = std::numeric_limits<float>::infinity();
			int bestSplit = -1;
			for (int i = 0; i < kSAHBins - 1; ++i)
			{
				if (leftCounts[i] == 0 || rightCounts[i] == 0)
				{
					continue;
				}

				const float cost = static_cast<float>(leftCounts[i]) * ComputeSurfaceArea(leftBounds[i])
					+ static_cast<float>(rightCounts[i]) * ComputeSurfaceArea(rightBounds[i]);
				if (cost < bestCost)
				{
					bestCost = cost;
					bestSplit = i;
				}
			}

			if (bestSplit >= 0)
			{
				auto midIt = std::partition(leafIndices.begin() + begin, leafIndices.begin() + end, [&](int leafIndex)
				{
					const AABB& aabb = nodes[leafIndex].aabb;
					const glm::vec3 center = 0.5f * (aabb.min + aabb.max);
					float t = (center[axis] - centroidBox.min[axis]) / axisExtent;
					t = glm::clamp(t, 0.0f, 0.999999f);
					const int binIndex = glm::clamp(static_cast<int>(t * static_cast<float>(kSAHBins)), 0, kSAHBins - 1);
					return binIndex <= bestSplit;
				});

				mid = static_cast<int>(midIt - leafIndices.begin());
				if (mid == begin || mid == end)
				{
					mid = begin + count / 2;
				}
			}
		}

		if (mid == begin || mid == end)
		{
			mid = begin + count / 2;
			std::nth_element(leafIndices.begin() + begin,
				leafIndices.begin() + mid,
				leafIndices.begin() + end,
				[&](int a, int b)
			{
				float ca = 0.5f * (nodes[a].aabb.min[axis] + nodes[a].aabb.max[axis]);
				float cb = 0.5f * (nodes[b].aabb.min[axis] + nodes[b].aabb.max[axis]);
				return ca < cb;
			});
		}

		const int idx = static_cast<int>(nodes.size());
		nodes.emplace_back();

		const int left = BuildRecursive(leafIndices, begin, mid);
		const int right = BuildRecursive(leafIndices, mid, end);

		nodes[idx].left = left;
		nodes[idx].right = right;
		nodes[idx].parent = -1;

		nodes[left].parent = idx;
		nodes[right].parent = idx;

		nodes[idx].aabb = MergeAABBs(nodes[left].aabb, nodes[right].aabb);
		nodes[idx].fatAABB = MergeAABBs(nodes[left].fatAABB, nodes[right].fatAABB);
		nodes[idx].entity = entt::null;
		nodes[idx].hasCullHistory = false;

		return idx;
	}

	void SceneBVH::FullRebuild()
	{
		nodes.clear();
		wideNodes.clear();
		entityToLeaf.clear();
		root = -1;
		wideRoot = -1;

		size_t estimatedLeafCount = 0;
		estimatedLeafCount += registry.view<Transform, Material>().size_hint();
		estimatedLeafCount += registry.view<Transform, CompositeMaterial>().size_hint();
		nodes.reserve(estimatedLeafCount * 2 + 1);

		std::vector<int> leafIndices;
		leafIndices.reserve(estimatedLeafCount);

		auto view = registry.view<Transform, Material>();
		for (entt::entity e : view)
		{
			const auto& tf = view.get<Transform>(e);
			if (tf.GetTransformSpace() != TransformSpace::World)
			{
				continue;
			}

			const auto& material = view.get<Material>(e).data;
			if (!material || !material->mesh || !material->mesh->meshBufferData)
			{
				continue;
			}

			BVHNode leaf;
			leaf.entity = e;
			leaf.aabb = CalculateWorldAABB(e, glm::vec3(material->mesh->meshBufferData->aabbMin), glm::vec3(material->mesh->meshBufferData->aabbMax), tf);
			leaf.fatAABB = MakeFatAABB(leaf.aabb);

			const int idx = static_cast<int>(nodes.size());
			nodes.emplace_back(std::move(leaf));
			entityToLeaf[e] = idx;
			leafIndices.push_back(idx);
		}

		auto compositeView = registry.view<Transform, CompositeMaterial>();
		for (entt::entity e : compositeView)
		{
			const auto& tf = compositeView.get<Transform>(e);
			if (tf.GetTransformSpace() != TransformSpace::World)
			{
				continue;
			}

			const auto& comp = compositeView.get<CompositeMaterial>(e);
			glm::vec3 localMin;
			glm::vec3 localMax;
			if (!HasRenderableSubMaterials(comp, localMin, localMax))
			{
				continue;
			}

			BVHNode leaf;
			leaf.entity = e;
			leaf.aabb = CalculateWorldAABB(e, localMin, localMax, tf);
			leaf.fatAABB = MakeFatAABB(leaf.aabb);

			const int idx = static_cast<int>(nodes.size());
			nodes.emplace_back(std::move(leaf));
			entityToLeaf[e] = idx;
			leafIndices.push_back(idx);
		}

		if (leafIndices.empty())
		{
			leafToWideParent.clear();
			leafToWideSlot.clear();
			return;
		}

		root = BuildRecursive(leafIndices, 0, static_cast<int>(leafIndices.size()));
		BuildWideHierarchy();
	}

	void SceneBVH::RefitBinaryAncestors(int leafIndex)
	{
		int nodeIndex = leafIndex;
		while (nodeIndex != -1)
		{
			BVHNode& node = nodes[nodeIndex];
			node.hasCullHistory = false;

			if (!node.IsLeaf())
			{
				node.aabb = MergeAABBs(nodes[node.left].aabb, nodes[node.right].aabb);
				node.fatAABB = MergeAABBs(nodes[node.left].fatAABB, nodes[node.right].fatAABB);
			}

			nodeIndex = node.parent;
		}
	}

	void SceneBVH::SetWideChildBounds(int wideIndex, uint8_t childSlot, const AABB& aabb)
	{
		WideNode& node = wideNodes[wideIndex];
		node.minX[childSlot] = aabb.min.x;
		node.minY[childSlot] = aabb.min.y;
		node.minZ[childSlot] = aabb.min.z;
		node.maxX[childSlot] = aabb.max.x;
		node.maxY[childSlot] = aabb.max.y;
		node.maxZ[childSlot] = aabb.max.z;
	}

	void SceneBVH::UpdateWideNodeBoundsFromChildren(int wideIndex)
	{
		WideNode& node = wideNodes[wideIndex];
		node.traversalAABB.min = glm::vec3(FLT_MAX);
		node.traversalAABB.max = glm::vec3(-FLT_MAX);
		node.hasCullHistory = false;
		node.lastVisibleMask = 0;
		node.lastFullyInsideMask = 0;

		for (uint8_t i = 0; i < node.childCount; ++i)
		{
			if (node.childRef[i] == InvalidWideChild)
			{
				continue;
			}

			node.traversalAABB.min.x = glm::min(node.traversalAABB.min.x, node.minX[i]);
			node.traversalAABB.min.y = glm::min(node.traversalAABB.min.y, node.minY[i]);
			node.traversalAABB.min.z = glm::min(node.traversalAABB.min.z, node.minZ[i]);
			node.traversalAABB.max.x = glm::max(node.traversalAABB.max.x, node.maxX[i]);
			node.traversalAABB.max.y = glm::max(node.traversalAABB.max.y, node.maxY[i]);
			node.traversalAABB.max.z = glm::max(node.traversalAABB.max.z, node.maxZ[i]);
		}
	}

	int SceneBVH::BuildWideRecursive(int binaryNodeIndex, int parentWideIndex, uint8_t parentSlot)
	{
		const int wideIndex = static_cast<int>(wideNodes.size());
		wideNodes.emplace_back();

		wideNodes[wideIndex].parent = parentWideIndex;
		wideNodes[wideIndex].parentSlot = parentSlot;

		std::vector<int> frontier;
		frontier.reserve(kWideNodeArity);

		const BVHNode& rootNode = nodes[binaryNodeIndex];
		if (rootNode.IsLeaf())
		{
			frontier.push_back(binaryNodeIndex);
		}
		else
		{
			frontier.push_back(rootNode.left);
			frontier.push_back(rootNode.right);
		}

		while (static_cast<int>(frontier.size()) < kWideNodeArity)
		{
			int expandCandidateIndex = -1;
			float expandCandidateArea = -1.0f;

			for (int i = 0; i < static_cast<int>(frontier.size()); ++i)
			{
				const BVHNode& candidate = nodes[frontier[i]];
				if (candidate.IsLeaf())
				{
					continue;
				}

				const float candidateArea = ComputeSurfaceArea(GetTraversalAABB(candidate));
				if (candidateArea > expandCandidateArea)
				{
					expandCandidateArea = candidateArea;
					expandCandidateIndex = i;
				}
			}

			if (expandCandidateIndex == -1)
			{
				break;
			}

			const int candidateBinaryIndex = frontier[expandCandidateIndex];
			frontier[expandCandidateIndex] = nodes[candidateBinaryIndex].left;
			frontier.push_back(nodes[candidateBinaryIndex].right);
		}

		wideNodes[wideIndex].childCount = static_cast<uint8_t>(frontier.size());
		for (uint8_t i = 0; i < kWideNodeArity; ++i)
		{
			wideNodes[wideIndex].childTraversalOrder[i] = i;
		}

		for (uint8_t i = 0; i < wideNodes[wideIndex].childCount; ++i)
		{
			const int childBinaryIndex = frontier[i];
			const BVHNode& childBinaryNode = nodes[childBinaryIndex];
			const AABB& childAABB = childBinaryNode.IsLeaf() ? childBinaryNode.fatAABB : GetTraversalAABB(childBinaryNode);
			SetWideChildBounds(wideIndex, i, childAABB);

			if (childBinaryNode.IsLeaf())
			{
				wideNodes[wideIndex].childRef[i] = EncodeWideLeaf(childBinaryIndex);
				if (childBinaryIndex >= 0 && childBinaryIndex < static_cast<int>(leafToWideParent.size()))
				{
					leafToWideParent[childBinaryIndex] = wideIndex;
					leafToWideSlot[childBinaryIndex] = i;
				}
			}
			else
			{
				wideNodes[wideIndex].childRef[i] = BuildWideRecursive(childBinaryIndex, wideIndex, i);
			}
		}

		UpdateWideNodeBoundsFromChildren(wideIndex);
		return wideIndex;
	}

	void SceneBVH::BuildWideHierarchy()
	{
		wideNodes.clear();
		wideRoot = -1;
		leafToWideParent.assign(nodes.size(), -1);
		leafToWideSlot.assign(nodes.size(), 0xFF);

		if (root == -1)
		{
			return;
		}

		wideNodes.reserve(nodes.size());
		wideRoot = BuildWideRecursive(root, -1, 0xFF);
	}

	void SceneBVH::RefitWideAncestorsFromLeaf(int leafIndex)
	{
		if (leafIndex < 0 || leafIndex >= static_cast<int>(leafToWideParent.size()))
		{
			return;
		}

		int wideIndex = leafToWideParent[leafIndex];
		if (wideIndex == -1)
		{
			return;
		}

		const uint8_t slot = leafToWideSlot[leafIndex];
		SetWideChildBounds(wideIndex, slot, nodes[leafIndex].fatAABB);
		UpdateWideNodeBoundsFromChildren(wideIndex);

		while (wideNodes[wideIndex].parent != -1)
		{
			const int parentWide = wideNodes[wideIndex].parent;
			const uint8_t parentSlot = wideNodes[wideIndex].parentSlot;
			SetWideChildBounds(parentWide, parentSlot, wideNodes[wideIndex].traversalAABB);
			UpdateWideNodeBoundsFromChildren(parentWide);
			wideIndex = parentWide;
		}
	}

	const AABB& SceneBVH::GetTraversalAABB(const BVHNode& node) const
	{
		return node.IsLeaf() ? node.aabb : node.fatAABB;
	}

	AABBFrustumClassification SceneBVH::ClassifyNode(const BVHNode& node, const Frustum& frustum, const AABB& aabb) const
	{
		const uint64_t frustumRevision = Frustum::GetRevision();
		if (node.hasCullHistory
			&& node.lastFrustumRevision == frustumRevision
			&& node.lastCullAABBMin == aabb.min
			&& node.lastCullAABBMax == aabb.max)
		{
			return static_cast<AABBFrustumClassification>(node.lastClassification);
		}

		uint8_t planeHint = node.lastRejectedPlane;
		const AABBFrustumClassification classification = frustum.ClassifyAABB(aabb, planeHint);

		node.lastRejectedPlane = planeHint;
		node.lastFrustumRevision = frustumRevision;
		node.lastCullAABBMin = aabb.min;
		node.lastCullAABBMax = aabb.max;
		node.lastClassification = static_cast<uint8_t>(classification);
		node.lastVisible = classification != AABBFrustumClassification::Outside;
		node.hasCullHistory = true;

		return classification;
	}

	AABBFrustumClassification SceneBVH::ClassifyWideNode(const WideNode& node, const Frustum& frustum) const
	{
		const uint64_t frustumRevision = Frustum::GetRevision();
		if (node.hasCullHistory
			&& node.lastFrustumRevision == frustumRevision
			&& node.lastCullAABBMin == node.traversalAABB.min
			&& node.lastCullAABBMax == node.traversalAABB.max)
		{
			return static_cast<AABBFrustumClassification>(node.lastClassification);
		}

		uint8_t planeHint = node.lastRejectedPlane;
		const AABBFrustumClassification classification = frustum.ClassifyAABB(node.traversalAABB, planeHint);

		node.lastRejectedPlane = planeHint;
		node.lastFrustumRevision = frustumRevision;
		node.lastCullAABBMin = node.traversalAABB.min;
		node.lastCullAABBMax = node.traversalAABB.max;
		node.lastClassification = static_cast<uint8_t>(classification);
		node.lastVisible = classification != AABBFrustumClassification::Outside;
		node.hasCullHistory = true;
		return classification;
	}

	uint8_t SceneBVH::GetWideNodeVisibleMask(const WideNode& node, const Frustum& frustum, uint8_t* outFullyInsideMask) const
	{
		const uint8_t activeChildMask = static_cast<uint8_t>((1u << node.childCount) - 1u);
		uint8_t visibleMask = activeChildMask;
		uint8_t fullyInsideMask = activeChildMask;
		uint8_t planeOrder[kFrustumPlaneCount]{ 0, 1, 2, 3, 4, 5 };
		BuildPlaneTraversalOrder(node.lastRejectedPlane, planeOrder);

	#if SWIM_BVH_USE_SSE
		const __m128 zero = _mm_setzero_ps();
		for (int planePass = 0; planePass < kFrustumPlaneCount; ++planePass)
		{
			const int planeIndex = planeOrder[planePass];
			const glm::vec4& plane = frustum.planes[planeIndex];
			const __m128 px = _mm_set1_ps(plane.x);
			const __m128 py = _mm_set1_ps(plane.y);
			const __m128 pz = _mm_set1_ps(plane.z);
			const __m128 pw = _mm_set1_ps(plane.w);

			const __m128 outsideX = _mm_load_ps((plane.x >= 0.0f) ? node.maxX : node.minX);
			const __m128 outsideY = _mm_load_ps((plane.y >= 0.0f) ? node.maxY : node.minY);
			const __m128 outsideZ = _mm_load_ps((plane.z >= 0.0f) ? node.maxZ : node.minZ);

			__m128 outsideDistance = _mm_add_ps(_mm_mul_ps(px, outsideX), _mm_mul_ps(py, outsideY));
			outsideDistance = _mm_add_ps(outsideDistance, _mm_mul_ps(pz, outsideZ));
			outsideDistance = _mm_add_ps(outsideDistance, pw);

			const uint8_t outsideMask = static_cast<uint8_t>(_mm_movemask_ps(_mm_cmplt_ps(outsideDistance, zero))) & visibleMask;
			visibleMask = static_cast<uint8_t>(visibleMask & ~outsideMask);
			fullyInsideMask = static_cast<uint8_t>(fullyInsideMask & visibleMask);
			if (visibleMask == 0)
			{
				break;
			}

			const __m128 insideX = _mm_load_ps((plane.x >= 0.0f) ? node.minX : node.maxX);
			const __m128 insideY = _mm_load_ps((plane.y >= 0.0f) ? node.minY : node.maxY);
			const __m128 insideZ = _mm_load_ps((plane.z >= 0.0f) ? node.minZ : node.maxZ);

			__m128 insideDistance = _mm_add_ps(_mm_mul_ps(px, insideX), _mm_mul_ps(py, insideY));
			insideDistance = _mm_add_ps(insideDistance, _mm_mul_ps(pz, insideZ));
			insideDistance = _mm_add_ps(insideDistance, pw);

			const uint8_t insideMask = static_cast<uint8_t>(_mm_movemask_ps(_mm_cmpge_ps(insideDistance, zero)));
			fullyInsideMask = static_cast<uint8_t>(fullyInsideMask & insideMask);
		}
	#else
		for (uint8_t childIndex = 0; childIndex < node.childCount; ++childIndex)
		{
			AABB childAABB;
			childAABB.min = glm::vec3(node.minX[childIndex], node.minY[childIndex], node.minZ[childIndex]);
			childAABB.max = glm::vec3(node.maxX[childIndex], node.maxY[childIndex], node.maxZ[childIndex]);

			uint8_t planeHint = node.lastRejectedPlane;
			const AABBFrustumClassification classification = frustum.ClassifyAABB(childAABB, planeHint);
			if (classification == AABBFrustumClassification::Outside)
			{
				visibleMask = static_cast<uint8_t>(visibleMask & ~(1u << childIndex));
				fullyInsideMask = static_cast<uint8_t>(fullyInsideMask & ~(1u << childIndex));
				continue;
			}

			if (classification != AABBFrustumClassification::Inside)
			{
				fullyInsideMask = static_cast<uint8_t>(fullyInsideMask & ~(1u << childIndex));
			}
		}
	#endif

		fullyInsideMask = static_cast<uint8_t>(fullyInsideMask & visibleMask);
		if (outFullyInsideMask != nullptr)
		{
			*outFullyInsideMask = fullyInsideMask;
		}

		return visibleMask;
	}

	void SceneBVH::CollectWideTraversalOrder(const WideNode& node, uint8_t visibleMask, uint8_t fullyInsideMask, uint8_t* outOrder, uint8_t& outCount) const
	{
		outCount = 0;

		const uint8_t activeMask = static_cast<uint8_t>((1u << node.childCount) - 1u);
		const uint8_t clampedVisibleMask = static_cast<uint8_t>(visibleMask & activeMask);
		const uint8_t clampedFullyInsideMask = static_cast<uint8_t>(fullyInsideMask & clampedVisibleMask);
		const uint8_t visibleIntersectingMask = static_cast<uint8_t>(clampedVisibleMask & ~clampedFullyInsideMask);
		const uint8_t invisibleMask = static_cast<uint8_t>(activeMask & ~clampedVisibleMask);

		uint8_t orderedAll[4]{ 0, 1, 2, 3 };
		uint8_t orderedAllCount = 0;
		uint8_t emittedMask = 0;

		auto tryAppend = [&](uint8_t mask, bool visible)
		{
			auto appendIfNeeded = [&](uint8_t childIndex)
			{
				if (childIndex >= node.childCount)
				{
					return;
				}

				const uint8_t bit = static_cast<uint8_t>(1u << childIndex);
				if ((mask & bit) == 0 || (emittedMask & bit) != 0)
				{
					return;
				}

				emittedMask = static_cast<uint8_t>(emittedMask | bit);
				orderedAll[orderedAllCount++] = childIndex;
				if (visible)
				{
					outOrder[outCount++] = childIndex;
				}
			};

			for (uint8_t orderIndex = 0; orderIndex < node.childCount; ++orderIndex)
			{
				appendIfNeeded(node.childTraversalOrder[orderIndex]);
			}

			for (uint8_t childIndex = 0; childIndex < node.childCount; ++childIndex)
			{
				appendIfNeeded(childIndex);
			}
		};

		tryAppend(clampedFullyInsideMask, true);
		tryAppend(visibleIntersectingMask, true);
		tryAppend(invisibleMask, false);

		for (uint8_t i = 0; i < node.childCount; ++i)
		{
			node.childTraversalOrder[i] = orderedAll[i];
		}

		node.lastVisibleMask = clampedVisibleMask;
		node.lastFullyInsideMask = clampedFullyInsideMask;
	}

	bool SceneBVH::PushWideRootIfVisible(const Frustum& frustum, WideTraversalItem* stack, int& stackSize) const
	{
		if (wideRoot == -1)
		{
			return false;
		}

		const WideNode& rootNode = wideNodes[wideRoot];
		const AABBFrustumClassification classification = ClassifyWideNode(rootNode, frustum);
		if (classification == AABBFrustumClassification::Outside)
		{
			return false;
		}

		if (stackSize >= WideTraversalStackMax)
		{
			return false;
		}

		stack[stackSize++] = { wideRoot, classification == AABBFrustumClassification::Inside };
		return true;
	}

	inline void SceneBVH::PushIfVisible(int nodeIdx, const Frustum& frustum, bool parentFullyInside, std::vector<std::pair<int, bool>>& stack) const
	{
		const BVHNode& node = nodes[nodeIdx];
		if (node.entity == entt::null && node.IsLeaf())
		{
			return;
		}

		if (parentFullyInside)
		{
			stack.emplace_back(nodeIdx, true);
			return;
		}

		if (node.IsLeaf())
		{
			if (registry.valid(node.entity) && registry.any_of<Transform>(node.entity) && registry.any_of<FrustumCullCache>(node.entity))
			{
				const Transform& tf = registry.get<Transform>(node.entity);
				auto& cache = registry.get<FrustumCullCache>(node.entity);
				if (frustum.IsVisibleCached(cache, node.aabb, tf.GetWorldVersion()))
				{
					stack.emplace_back(nodeIdx, false);
				}
				return;
			}
		}

		const AABB& traversalAABB = GetTraversalAABB(node);
		const AABBFrustumClassification classification = ClassifyNode(node, frustum, traversalAABB);
		if (classification != AABBFrustumClassification::Outside)
		{
			stack.emplace_back(nodeIdx, classification == AABBFrustumClassification::Inside);
		}
	}

	void SceneBVH::TraverseWideSubtree(int wideIndex, bool fullyInside, const Frustum& frustum, std::vector<entt::entity>& outVisible) const
	{
		WideTraversalItem stack[WideTraversalStackMax];
		int stackSize = 0;
		stack[stackSize++] = { wideIndex, fullyInside };

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
							outVisible.push_back(entity);
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
						outVisible.push_back(entity);
					}
				}
				else if (stackSize < WideTraversalStackMax)
				{
					stack[stackSize++] = { childRef, (fullyInsideMask & bit) != 0 };
				}
			}
		}
	}

	void SceneBVH::QueryFrustum(const Frustum& frustum, std::vector<entt::entity>& outVisible) const
	{
		outVisible.clear();
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

		const WideTraversalItem rootItem = stack[--stackSize];
		TraverseWideSubtree(rootItem.wideIndex, rootItem.fullyInside, frustum, outVisible);

	#ifdef _SWIM_DEBUG
		constexpr bool debugPrint = false;
		if constexpr (debugPrint)
		{
			auto view = registry.view<Transform, Material>();
			int totalEntitiesInScene = static_cast<int>(view.size_hint());
			int entitiesVisible = static_cast<int>(outVisible.size());
			int estimatedEntitiesCulled = totalEntitiesInScene - entitiesVisible;
			std::cout << "Estimate Culled " << estimatedEntitiesCulled << " of " << totalEntitiesInScene << " entities\n";
		}
	#endif // _DEBUG
	}

	void SceneBVH::BuildGpuWideSnapshot(
		std::vector<GpuWideSnapshotNode>& outNodes,
		std::vector<GpuWideSnapshotLeaf>& outLeaves,
		uint32_t* outRootIndex,
		uint32_t* outMaxDepth
	) const
	{
		outNodes.clear();
		outLeaves.clear();

		if (outRootIndex)
		{
			*outRootIndex = 0;
		}

		if (outMaxDepth)
		{
			*outMaxDepth = 0;
		}

		if (wideRoot == -1 || wideNodes.empty())
		{
			return;
		}

		std::vector<int> denseLeafFromBinary(nodes.size(), -1);
		for (const WideNode& wideNode : wideNodes)
		{
			for (uint8_t childIndex = 0; childIndex < wideNode.childCount; ++childIndex)
			{
				const int childRef = wideNode.childRef[childIndex];
				if (!IsEncodedWideLeaf(childRef))
				{
					continue;
				}

				const int leafIndex = DecodeWideLeaf(childRef);
				if (leafIndex < 0 || leafIndex >= static_cast<int>(nodes.size()))
				{
					continue;
				}

				if (denseLeafFromBinary[leafIndex] != -1)
				{
					continue;
				}

				denseLeafFromBinary[leafIndex] = static_cast<int>(outLeaves.size());
				outLeaves.push_back(GpuWideSnapshotLeaf{ nodes[leafIndex].entity });
			}
		}

		outNodes.resize(wideNodes.size());
		for (size_t nodeIndex = 0; nodeIndex < wideNodes.size(); ++nodeIndex)
		{
			const WideNode& sourceNode = wideNodes[nodeIndex];
			GpuWideSnapshotNode& destNode = outNodes[nodeIndex];

			for (uint32_t lane = 0; lane < 4; ++lane)
			{
				destNode.minX[lane] = sourceNode.minX[lane];
				destNode.minY[lane] = sourceNode.minY[lane];
				destNode.minZ[lane] = sourceNode.minZ[lane];
				destNode.maxX[lane] = sourceNode.maxX[lane];
				destNode.maxY[lane] = sourceNode.maxY[lane];
				destNode.maxZ[lane] = sourceNode.maxZ[lane];
				destNode.childTraversalOrder[lane] = sourceNode.childTraversalOrder[lane];

				const int childRef = sourceNode.childRef[lane];
				if (IsEncodedWideLeaf(childRef))
				{
					const int leafIndex = DecodeWideLeaf(childRef);
					const int denseLeafIndex = (leafIndex >= 0 && leafIndex < static_cast<int>(denseLeafFromBinary.size())) ? denseLeafFromBinary[leafIndex] : -1;
					destNode.childRef[lane] = denseLeafIndex >= 0 ? EncodeWideLeaf(denseLeafIndex) : InvalidWideChild;
				}
				else
				{
					destNode.childRef[lane] = childRef;
				}
			}

			destNode.childCount = sourceNode.childCount;
		}

		if (outRootIndex)
		{
			*outRootIndex = static_cast<uint32_t>(wideRoot);
		}

		if (outMaxDepth)
		{
			uint32_t maxDepth = 0;
			std::vector<std::pair<int, uint32_t>> stack;
			stack.reserve(outNodes.size());
			stack.emplace_back(wideRoot, 1u);

			while (!stack.empty())
			{
				auto [wideIndex, depth] = stack.back();
				stack.pop_back();
				maxDepth = std::max(maxDepth, depth);

				if (wideIndex < 0 || wideIndex >= static_cast<int>(wideNodes.size()))
				{
					continue;
				}

				const WideNode& wideNode = wideNodes[wideIndex];
				for (uint8_t childIndex = 0; childIndex < wideNode.childCount; ++childIndex)
				{
					const int childRef = wideNode.childRef[childIndex];
					if (childRef == InvalidWideChild || IsEncodedWideLeaf(childRef))
					{
						continue;
					}

					stack.emplace_back(childRef, depth + 1u);
				}
			}

			*outMaxDepth = maxDepth;
		}
	}

	void SceneBVH::QueryFrustumParallel(const Frustum& frustum, std::vector<entt::entity>& outVisible) const
	{
		outVisible.clear();
		if (wideRoot == -1)
		{
			return;
		}

		if constexpr (!RenderCpuJobConfig::Enabled)
		{
			QueryFrustum(frustum, outVisible);
			return;
		}

		const size_t workerSlots = GetRenderParallelWorkerSlots();
		if (workerSlots <= 1)
		{
			QueryFrustum(frustum, outVisible);
			return;
		}

		WideTraversalItem stack[WideTraversalStackMax];
		int stackSize = 0;
		if (!PushWideRootIfVisible(frustum, stack, stackSize))
		{
			return;
		}

		const size_t targetSeedCount = std::min<size_t>(workerSlots * 2, 64);
		EnsureParallelQueryScratch(workerSlots, targetSeedCount);
		std::vector<WideTraversalItem>& seedItems = parallelSeedItemsScratch;
		std::vector<entt::entity>& directlyVisible = parallelDirectVisibleScratch;

		while (stackSize > 0 && seedItems.size() < targetSeedCount)
		{
			const WideTraversalItem item = stack[--stackSize];
			const WideNode& node = wideNodes[item.wideIndex];

			if (item.fullyInside || node.childCount <= 1)
			{
				seedItems.push_back(item);
				continue;
			}

			uint8_t fullyInsideMask = 0;
			const uint8_t visibleMask = GetWideNodeVisibleMask(node, frustum, &fullyInsideMask);

			uint8_t traversalOrder[4]{ 0, 1, 2, 3 };
			uint8_t traversalCount = 0;
			CollectWideTraversalOrder(node, visibleMask, fullyInsideMask, traversalOrder, traversalCount);

			if (traversalCount <= 1 || seedItems.size() + static_cast<size_t>(traversalCount) > targetSeedCount)
			{
				seedItems.push_back(item);
				continue;
			}

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
						directlyVisible.push_back(entity);
					}
				}
				else if (stackSize < WideTraversalStackMax)
				{
					stack[stackSize++] = { childRef, (fullyInsideMask & bit) != 0 };
				}
			}
		}

		while (stackSize > 0)
		{
			seedItems.push_back(stack[--stackSize]);
		}

		const size_t minParallelSeedCount = std::max<size_t>(workerSlots * 2, 8);
		if (seedItems.size() < minParallelSeedCount)
		{
			outVisible.reserve(directlyVisible.size() + seedItems.size() * 4);
			outVisible.insert(outVisible.end(), directlyVisible.begin(), directlyVisible.end());
			for (const WideTraversalItem& item : seedItems)
			{
				TraverseWideSubtree(item.wideIndex, item.fullyInside, frustum, outVisible);
			}
			return;
		}

		parallelVisibleScratch[0].visible.clear();
		parallelVisibleScratch[0].visible.insert(parallelVisibleScratch[0].visible.end(), directlyVisible.begin(), directlyVisible.end());
		for (size_t slot = 1; slot < workerSlots; ++slot)
		{
			parallelVisibleScratch[slot].visible.clear();
		}

		ParallelForRender(seedItems.size(), 1, [&](size_t begin, size_t end, uint32_t workerIndex)
		{
			std::vector<entt::entity>& localVisible = parallelVisibleScratch[workerIndex].visible;
			for (size_t i = begin; i < end; ++i)
			{
				const WideTraversalItem& item = seedItems[i];
				TraverseWideSubtree(item.wideIndex, item.fullyInside, frustum, localVisible);
			}
		});

		size_t totalVisible = 0;
		for (size_t slot = 0; slot < workerSlots; ++slot)
		{
			totalVisible += parallelVisibleScratch[slot].visible.size();
		}

		outVisible.reserve(totalVisible);
		for (size_t slot = 0; slot < workerSlots; ++slot)
		{
			std::vector<entt::entity>& localVisible = parallelVisibleScratch[slot].visible;
			outVisible.insert(outVisible.end(), localVisible.begin(), localVisible.end());
		}
	}

	bool SceneBVH::IsFullyVisible(const Frustum& frustum) const
	{
		if (wideRoot == -1)
		{
			return false;
		}

		return ClassifyWideNode(wideNodes[wideRoot], frustum) == AABBFrustumClassification::Inside;
	}

	void SceneBVH::DebugRender()
	{
		if (debugDrawer == nullptr || !debugDrawer->IsEnabled())
		{
			return;
		}

		constexpr bool drawInternalNodes = true;
		constexpr bool drawCompositeSubmeshes = true;

		// === Internal BVH nodes (non-leaf) ===
		if constexpr (drawInternalNodes)
		{
			for (size_t i = 0; i < nodes.size(); ++i)
			{
				const BVHNode& node = nodes[i];
				if (node.IsLeaf())
				{
					continue;
				}

				const AABB& a = node.fatAABB;
				debugDrawer->SubmitWireframeBoxAABB(a.min, a.max,
					{ 1.0f, 0.0f, 0.0f, 1.0f }, // red
					false, // no fill
					{ 0.0f, 0.0f, 0.0f, 1.0f }, // no fill color
					glm::vec2(10.0f), // wireframe line width
					glm::vec2(0.0f), // corner radius of 0 (none)
					0, // world space bit
					SceneDebugDraw::MeshBoxType::BevelledCube // use bevelled mesh
				);
			}
		}

		// === Entity AABBs (leaf nodes) ===
		for (const auto& [entity, leafIndex] : entityToLeaf)
		{
			const BVHNode& leaf = nodes[leafIndex];
			const AABB& a = leaf.aabb;

			glm::vec4 color;

			if (registry.all_of<Material>(entity))
			{
				color = { 0.2f, 1.0f, 0.2f, 1.0f }; // green
			}
			else if (registry.all_of<CompositeMaterial>(entity))
			{
				color = { 0.2f, 0.6f, 1.0f, 1.0f }; // blue
			}
			else
			{
				color = { 1.0f, 1.0f, 0.0f, 1.0f }; // yellow
			}

			debugDrawer->SubmitWireframeBoxAABB(a.min, a.max,
				color,
				false, // no fill
				{ 0.0f, 0.0f, 0.0f, 1.0f }, // no fill color
				glm::vec2(10.0f), // wireframe line width
				glm::vec2(0.0f), // corner radius of 0 (none)
				0, // world space bit
				SceneDebugDraw::MeshBoxType::BevelledCube // use bevelled mesh
			);

			// === Optional: draw individual submesh AABBs of composites ===
			if constexpr (drawCompositeSubmeshes)
			{
				if (registry.all_of<Transform, CompositeMaterial>(entity))
				{
					const auto& tf = registry.get<Transform>(entity);
					const auto& comp = registry.get<CompositeMaterial>(entity);

					const glm::mat4& model = tf.GetWorldMatrix(registry);

					for (const auto& mat : comp.subMaterials)
					{
						if (!mat || !mat->mesh || !mat->mesh->meshBufferData)
						{
							continue;
						}

						const glm::vec3 min = glm::vec3(mat->mesh->meshBufferData->aabbMin);
						const glm::vec3 max = glm::vec3(mat->mesh->meshBufferData->aabbMax);

						// Transform AABB to world space (same 8 corner expansion used in your CalculateWorldAABB)
						glm::vec3 worldMin = glm::vec3(model * glm::vec4(min, 1.0f));
						glm::vec3 worldMax = worldMin;

						EXPAND_CORNER(max.x, min.y, min.z);
						EXPAND_CORNER(min.x, max.y, min.z);
						EXPAND_CORNER(max.x, max.y, min.z);
						EXPAND_CORNER(min.x, min.y, max.z);
						EXPAND_CORNER(max.x, min.y, max.z);
						EXPAND_CORNER(min.x, max.y, max.z);
						EXPAND_CORNER(max.x, max.y, max.z);

						debugDrawer->SubmitWireframeBoxAABB(worldMin, worldMax,
							{ 1.0f, 0.5f, 1.0f, 1.0f },
							false, // no fill
							{ 0.0f, 0.0f, 0.0f, 1.0f }, // no fill color
							glm::vec2(10.0f), // wireframe line width
							glm::vec2(0.0f), // corner radius of 0 (none)
							0, // world space bit
							SceneDebugDraw::MeshBoxType::BevelledCube // use bevelled mesh
						);
					}
				}
			}
		}
	}

	void SceneBVH::UpdateIfNeeded(entt::observer& frustumObserver)
	{
		bool needsUpdate = !frustumObserver.empty();

		if (!needsUpdate && !Transform::GetDirtyEntities().empty())
		{
			needsUpdate = true;
		}

		if (!needsUpdate && topologyObserver.size() > 0)
		{
			needsUpdate = true;
		}

		if (needsUpdate || forceUpdate)
		{
			Update();
			forceUpdate = false;
		}
	}

	void SceneBVH::RemoveEntity(entt::entity entity)
	{
		auto it = entityToLeaf.find(entity);
		if (it == entityToLeaf.end())
		{
			return;
		}

		const int idx = it->second;

		// Tombstone this leaf: keep array topology intact for this frame.
		nodes[idx].entity = entt::null;
		nodes[idx].aabb.min = glm::vec3(FLT_MAX);
		nodes[idx].aabb.max = glm::vec3(-FLT_MAX);
		nodes[idx].fatAABB.min = glm::vec3(FLT_MAX);
		nodes[idx].fatAABB.max = glm::vec3(-FLT_MAX);
		nodes[idx].hasCullHistory = false;

		if (idx >= 0 && idx < static_cast<int>(leafToWideParent.size()))
		{
			leafToWideParent[idx] = -1;
			leafToWideSlot[idx] = 0xFF;
		}

		entityToLeaf.erase(it);

		// Ensure we rebuild/refit before anyone traverses.
		forceUpdate = true;
	}

	entt::entity SceneBVH::RayCastClosestHit
	(
		const Ray& ray,
		float tMin,
		float tMax,
		float* outTHit
	) const
	{
		if (outTHit)
		{
			*outTHit = std::numeric_limits<float>::infinity();
		}

		if (root == -1)
		{
			return entt::null;
		}

		static constexpr int STACK_MAX = 512;
		struct Item { int idx; float tnear; };
		Item stack[STACK_MAX];
		int sp = 0;

		float tRoot;
		if (!RayIntersectsAABB(ray, GetTraversalAABB(nodes[root]), tMin, tMax, tRoot))
		{
			return entt::null;
		}

		stack[sp++] = { root, tRoot };

		float bestT = std::numeric_limits<float>::infinity();
		entt::entity bestE = entt::null;

		while (sp)
		{
			const Item it = stack[--sp];

			// Prune entire subtree if this node is already beyond best
			if (it.tnear > bestT)
			{
				continue;
			}

			const BVHNode& node = nodes[it.idx];

			if (node.IsLeaf())
			{
				// Skip tombstones
				if (node.entity != entt::null)
				{
					// Use carried tnear as the leaf hit distance (no re-test)
					const float tLeaf = it.tnear;
					if (tLeaf >= tMin && tLeaf <= bestT)
					{
						bestT = tLeaf;
						bestE = node.entity;
					}
				}

				continue;
			}

			// Intersect children with tMax tightened to current best
			float tL, tR;
			const bool hitL = RayIntersectsAABB(ray, GetTraversalAABB(nodes[node.left]), tMin, bestT, tL);
			const bool hitR = RayIntersectsAABB(ray, GetTraversalAABB(nodes[node.right]), tMin, bestT, tR);

			if (hitL && hitR)
			{
				// Push farther first so nearer pops next
				const bool leftIsNear = (tL <= tR);
				const int  nearIdx = leftIsNear ? node.left : node.right;
				const int  farIdx = leftIsNear ? node.right : node.left;
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
				if (sp + 1 <= STACK_MAX) stack[sp++] = { node.left,  tL };
			}
			else if (hitR)
			{
				if (sp + 1 <= STACK_MAX) stack[sp++] = { node.right, tR };
			}
		}

		if (bestE != entt::null && outTHit)
		{
			*outTHit = bestT;
		}

		return bestE;
	}

	// I have more versions of IsAABBVisible, and I have no idea which one is fastest
	bool SceneBVH::IsAABBVisible(const Frustum& frustum, const AABB& aabb) const
	{
		return frustum.IsAABBVisible(aabb);
	}

}
