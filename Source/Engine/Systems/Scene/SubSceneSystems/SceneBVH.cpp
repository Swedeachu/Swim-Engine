#include "PCH.h"
#include "SceneBVH.h"
#include "Engine/Components/Material.h"
#include "Engine/Components/CompositeMaterial.h"
#include "Engine/Components/Transform.h"
#include "Engine/Components/Internal/FrustumCullCache.h"
#include "Engine/Systems/Renderer/Core/Meshes/Mesh.h"
#include "Engine/Systems/Renderer/Core/Camera/Frustum.h"

namespace Engine
{

	namespace
	{

		AABB EmptyAABB()
		{
			AABB aabb;
			aabb.min = glm::vec3(FLT_MAX);
			aabb.max = glm::vec3(-FLT_MAX);
			return aabb;
		}

		AABB MergeAABBs(const AABB& a, const AABB& b)
		{
			AABB merged;
			merged.min = glm::min(a.min, b.min);
			merged.max = glm::max(a.max, b.max);
			return merged;
		}

		void ExpandAABB(AABB& dst, const AABB& src)
		{
			dst.min = glm::min(dst.min, src.min);
			dst.max = glm::max(dst.max, src.max);
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

		AABB MakeFatAABB(const AABB& aabb, const AABB& previousAABB)
		{
			const glm::vec3 extents = glm::max(aabb.max - aabb.min, glm::vec3(0.0f));
			const glm::vec3 padding = glm::max(extents * 0.10f, glm::vec3(0.05f));

			const glm::vec3 deltaMin = aabb.min - previousAABB.min;
			const glm::vec3 deltaMax = aabb.max - previousAABB.max;

			AABB fat;
			fat.min = aabb.min - padding + glm::min(deltaMin * 1.25f, glm::vec3(0.0f));
			fat.max = aabb.max + padding + glm::max(deltaMax * 1.25f, glm::vec3(0.0f));
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

	}

	SceneBVH::SceneBVH(entt::registry& registry)
		: registry{ registry }
	{}

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
		bool anyLeafEscapedFatBounds = false;
		std::vector<int> escapedLeafIndices;

		// === Rebuild / refit check ===
		static constexpr float kRebuildThreshold = 0.15f;

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

		escapedLeafIndices.reserve(dirtyEntities.size());

		for (entt::entity e : dirtyEntities)
		{
			if (e == entt::null || !registry.valid(e) || !registry.any_of<Transform>(e))
			{
				continue;
			}

			const Transform& tf = registry.get<Transform>(e);
			if (tf.GetTransformSpace() != TransformSpace::World)
			{
				auto existing = entityToLeaf.find(e);
				if (existing != entityToLeaf.end())
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
				auto existing = entityToLeaf.find(e);
				if (existing != entityToLeaf.end())
				{
					RemoveEntity(e);
				}
				continue;
			}

			auto it = entityToLeaf.find(e);
			if (it == entityToLeaf.end())
			{
				BVHNode leaf;
				leaf.entity = e;
				leaf.aabb = worldAABB;
				leaf.fatAABB = MakeFatAABB(worldAABB);

				int newIdx = static_cast<int>(nodes.size());
				nodes.emplace_back(std::move(leaf));
				entityToLeaf[e] = newIdx;
				forceUpdate = true;
				continue;
			}

			BVHNode& leaf = nodes[it->second];
			const AABB previousAABB = leaf.aabb;
			leaf.aabb = worldAABB;
			leaf.hasCullHistory = false;

			if (!AABBInsideAABB(leaf.aabb, leaf.fatAABB))
			{
				leaf.fatAABB = MakeFatAABB(leaf.aabb, previousAABB);
				anyLeafEscapedFatBounds = true;
				escapedLeafIndices.push_back(it->second);
			}
		}

		if (forceUpdate)
		{
			FullRebuild();
			forceUpdate = false;
			topologyObserver.clear();
			return;
		}

		if (anyLeafEscapedFatBounds && root != -1)
		{
			const AABB preAABB = nodes[root].fatAABB;
			const uint64_t epoch = refitEpoch++;
			if (refitEpoch == 0)
			{
				refitEpoch = 1;
			}

			for (int leafIndex : escapedLeafIndices)
			{
				RefitAncestors(leafIndex, epoch);
			}

			const float preArea = ComputeSurfaceArea(preAABB);
			const float postArea = ComputeSurfaceArea(nodes[root].fatAABB);

			if (preArea > 0.0f)
			{
				const float expansion = postArea / preArea;
				if (expansion > 1.0f + kRebuildThreshold)
				{
					FullRebuild();
					forceUpdate = false;
					topologyObserver.clear();
					return;
				}
			}
		}
	}

	/**
	 * Recursively builds a binary tree in breadth-first order.
	 * leafIndices is permutation-stable: indices in nodes[] that refer
	 * to existing leaves. The function returns the index of the node it creates.
	 */
	int SceneBVH::BuildRecursive(std::vector<int>& leafIndices, int begin, int end)
	{
		const int count = end - begin;
		if (count == 1)
		{
			return leafIndices[begin]; // already a leaf
		}

		AABB centroidBox = EmptyAABB();
		for (int i = begin; i < end; ++i)
		{
			const AABB& aabb = nodes[leafIndices[i]].aabb;
			const glm::vec3 ctr = 0.5f * (aabb.min + aabb.max);
			centroidBox.min = glm::min(centroidBox.min, ctr);
			centroidBox.max = glm::max(centroidBox.max, ctr);
		}

		const glm::vec3 extents = centroidBox.max - centroidBox.min;
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

		// We lean heavily on binned SAH rather than median splitting.
		// We stay binary, but build a better tree than the old longest-axis median fallback.
		static constexpr int kBinCount = 8;
		float bestCost = FLT_MAX;
		int bestAxis = -1;
		int bestSplit = -1;

		if (count > 2)
		{
			for (int testAxis = 0; testAxis < 3; ++testAxis)
			{
				if (extents[testAxis] <= 1e-5f)
				{
					continue;
				}

				struct Bin
				{
					AABB bounds;
					int count = 0;
				};

				Bin bins[kBinCount];
				for (int i = 0; i < kBinCount; ++i)
				{
					bins[i].bounds = EmptyAABB();
				}

				const float axisMin = centroidBox.min[testAxis];
				const float axisScale = static_cast<float>(kBinCount) / extents[testAxis];

				for (int i = begin; i < end; ++i)
				{
					const AABB& aabb = nodes[leafIndices[i]].aabb;
					const float centroid = 0.5f * (aabb.min[testAxis] + aabb.max[testAxis]);
					int binIndex = static_cast<int>((centroid - axisMin) * axisScale);
					binIndex = std::clamp(binIndex, 0, kBinCount - 1);

					Bin& bin = bins[binIndex];
					if (bin.count == 0)
					{
						bin.bounds = aabb;
					}
					else
					{
						ExpandAABB(bin.bounds, aabb);
					}
					++bin.count;
				}

				AABB leftBounds[kBinCount - 1];
				AABB rightBounds[kBinCount - 1];
				int leftCounts[kBinCount - 1]{};
				int rightCounts[kBinCount - 1]{};

				AABB accumLeft = EmptyAABB();
				int accumLeftCount = 0;
				for (int i = 0; i < kBinCount - 1; ++i)
				{
					if (bins[i].count > 0)
					{
						if (accumLeftCount == 0)
						{
							accumLeft = bins[i].bounds;
						}
						else
						{
							ExpandAABB(accumLeft, bins[i].bounds);
						}
					}
					accumLeftCount += bins[i].count;
					leftBounds[i] = accumLeft;
					leftCounts[i] = accumLeftCount;
				}

				AABB accumRight = EmptyAABB();
				int accumRightCount = 0;
				for (int i = kBinCount - 1; i > 0; --i)
				{
					if (bins[i].count > 0)
					{
						if (accumRightCount == 0)
						{
							accumRight = bins[i].bounds;
						}
						else
						{
							ExpandAABB(accumRight, bins[i].bounds);
						}
					}
					accumRightCount += bins[i].count;
					rightBounds[i - 1] = accumRight;
					rightCounts[i - 1] = accumRightCount;
				}

				for (int split = 0; split < kBinCount - 1; ++split)
				{
					if (leftCounts[split] == 0 || rightCounts[split] == 0)
					{
						continue;
					}

					const float cost = ComputeSurfaceArea(leftBounds[split]) * static_cast<float>(leftCounts[split])
						+ ComputeSurfaceArea(rightBounds[split]) * static_cast<float>(rightCounts[split]);

					if (cost < bestCost)
					{
						bestCost = cost;
						bestAxis = testAxis;
						bestSplit = split;
					}
				}
			}
		}

		if (bestAxis != -1)
		{
			const float axisMin = centroidBox.min[bestAxis];
			const float axisScale = static_cast<float>(kBinCount) / glm::max(extents[bestAxis], 1e-5f);

			auto pivot = std::partition(leafIndices.begin() + begin,
				leafIndices.begin() + end,
				[&](int leafIndex)
			{
				const AABB& aabb = nodes[leafIndex].aabb;
				const float centroid = 0.5f * (aabb.min[bestAxis] + aabb.max[bestAxis]);
				int binIndex = static_cast<int>((centroid - axisMin) * axisScale);
				binIndex = std::clamp(binIndex, 0, kBinCount - 1);
				return binIndex <= bestSplit;
			});

			mid = static_cast<int>(pivot - leafIndices.begin());
			if (mid == begin || mid == end)
			{
				bestAxis = -1;
				mid = begin + count / 2;
			}
		}

		if (bestAxis == -1)
		{
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

		BVHNode internal;
		internal.left = BuildRecursive(leafIndices, begin, mid);
		internal.right = BuildRecursive(leafIndices, mid, end);

		const AABB& lTight = nodes[internal.left].aabb;
		const AABB& rTight = nodes[internal.right].aabb;
		internal.aabb = MergeAABBs(lTight, rTight);

		const AABB& lFat = nodes[internal.left].fatAABB;
		const AABB& rFat = nodes[internal.right].fatAABB;
		internal.fatAABB = MergeAABBs(lFat, rFat);

		int idx = static_cast<int>(nodes.size());
		nodes.emplace_back(std::move(internal));
		nodes[internal.left].parent = idx;
		nodes[internal.right].parent = idx;
		return idx;
	}

	void SceneBVH::FullRebuild()
	{
		nodes.clear();
		entityToLeaf.clear();

		auto materialView = registry.view<Transform, Material>();
		auto compositeView = registry.view<Transform, CompositeMaterial>();
		const size_t estimatedLeafCount = static_cast<size_t>(materialView.size_hint()) + static_cast<size_t>(compositeView.size_hint());

		nodes.reserve(estimatedLeafCount * 2 + 1);
		entityToLeaf.reserve(estimatedLeafCount + 1);

		std::vector<int> leafIndices;
		leafIndices.reserve(estimatedLeafCount);

		// === Rebuild for Material entities ===
		for (entt::entity e : materialView)
		{
			const auto& tf = materialView.get<Transform>(e);
			if (tf.GetTransformSpace() != TransformSpace::World) { continue; }

			const auto& mat = materialView.get<Material>(e).data;
			if (!mat || !mat->mesh || !mat->mesh->meshBufferData) { continue; }
			const auto& mesh = mat->mesh;

			BVHNode leaf;
			leaf.entity = e;
			leaf.aabb = CalculateWorldAABB(mesh, tf);
			leaf.fatAABB = MakeFatAABB(leaf.aabb);

			int idx = static_cast<int>(nodes.size());
			nodes.emplace_back(std::move(leaf));
			entityToLeaf[e] = idx;
			leafIndices.push_back(idx);
		}

		// === Rebuild for CompositeMaterial entities ===
		for (entt::entity e : compositeView)
		{
			const auto& tf = compositeView.get<Transform>(e);
			if (tf.GetTransformSpace() != TransformSpace::World) { continue; }

			const auto& comp = compositeView.get<CompositeMaterial>(e);
			if (comp.subMaterials.empty()) { continue; }

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

			int idx = static_cast<int>(nodes.size());
			nodes.emplace_back(std::move(leaf));
			entityToLeaf[e] = idx;
			leafIndices.push_back(idx);
		}

		if (leafIndices.empty())
		{
			root = -1;
			return;
		}

		root = BuildRecursive(leafIndices, 0, static_cast<int>(leafIndices.size()));
		nodes[root].parent = -1;
	}

	// Refit = cheap bottom-up pass that tightens boxes without changing topology
	void SceneBVH::Refit(int nodeIndex)
	{
		BVHNode& node = nodes[nodeIndex];
		if (node.IsLeaf())
		{
			return; // leaf already up-to-date
		}

		Refit(node.left);
		Refit(node.right);

		const AABB& lTight = nodes[node.left].aabb;
		const AABB& rTight = nodes[node.right].aabb;
		node.aabb = MergeAABBs(lTight, rTight);

		const AABB& lFat = nodes[node.left].fatAABB;
		const AABB& rFat = nodes[node.right].fatAABB;
		node.fatAABB = MergeAABBs(lFat, rFat);
		node.hasCullHistory = false;
	}

	void SceneBVH::RefitAncestors(int leafIndex, uint64_t epoch)
	{
		int nodeIndex = leafIndex;
		while (nodeIndex != -1)
		{
			BVHNode& node = nodes[nodeIndex];
			if (node.lastRefitEpoch == epoch)
			{
				break;
			}

			node.lastRefitEpoch = epoch;
			if (!node.IsLeaf())
			{
				const AABB& lTight = nodes[node.left].aabb;
				const AABB& rTight = nodes[node.right].aabb;
				node.aabb = MergeAABBs(lTight, rTight);

				const AABB& lFat = nodes[node.left].fatAABB;
				const AABB& rFat = nodes[node.right].fatAABB;
				node.fatAABB = MergeAABBs(lFat, rFat);
				node.hasCullHistory = false;
			}

			nodeIndex = node.parent;
		}
	}

	const AABB& SceneBVH::GetTraversalAABB(const BVHNode& node) const
	{
		return node.IsLeaf() ? node.aabb : node.fatAABB;
	}

	bool SceneBVH::IsNodeVisible(const BVHNode& node, const Frustum& frustum, const AABB& aabb) const
	{
		const uint64_t frustumRevision = Frustum::GetRevision();
		if (node.hasCullHistory
			&& node.lastFrustumRevision == frustumRevision
			&& node.lastCullAABBMin == aabb.min
			&& node.lastCullAABBMax == aabb.max)
		{
			return node.lastVisible;
		}

		uint8_t planeHint = node.lastRejectedPlane;
		const bool visible = frustum.IsAABBVisible(aabb, planeHint);

		node.lastRejectedPlane = planeHint;
		node.lastFrustumRevision = frustumRevision;
		node.lastCullAABBMin = aabb.min;
		node.lastCullAABBMax = aabb.max;
		node.lastVisible = visible;
		node.hasCullHistory = true;

		return visible;
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
		if (frustum.ContainsAABB(traversalAABB))
		{
			stack.emplace_back(nodeIdx, true);
			return;
		}

		if (IsNodeVisible(node, frustum, traversalAABB))
		{
			stack.emplace_back(nodeIdx, false);
		}
	}

	void SceneBVH::QueryFrustum(const Frustum& frustum, std::vector<entt::entity>& outVisible) const
	{
		outVisible.clear();
		if (root == -1)
		{
			return;
		}

		std::vector<std::pair<int, bool>> stack;
		stack.reserve(128); // was 32
		PushIfVisible(root, frustum, false, stack);

		while (!stack.empty())
		{
			const auto [idx, fullyInside] = stack.back();
			stack.pop_back();

			const BVHNode& n = nodes[idx];
			if (n.IsLeaf())
			{
				// Skip tombstones
				if (n.entity != entt::null)
				{
					outVisible.push_back(n.entity);
				}
			}
			else
			{
				PushIfVisible(n.left, frustum, fullyInside, stack);
				PushIfVisible(n.right, frustum, fullyInside, stack);
			}
		}

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

	bool SceneBVH::IsFullyVisible(const Frustum& frustum) const
	{
		if (root == -1)
		{
			return false;
		}

		return frustum.ContainsAABB(GetTraversalAABB(nodes[root]));
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
