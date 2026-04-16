#include "PCH.h"
#include "SceneBVH.h"
#include "Engine/Components/Material.h"
#include "Engine/Components/CompositeMaterial.h"
#include "Engine/Components/Transform.h"
#include "Engine/Components/Internal/FrustumCullCache.h"
#include "Engine/Systems/Renderer/Core/Meshes/Mesh.h"
#include "Engine/Systems/Renderer/Core/Camera/Frustum.h"

#include <unordered_set>

namespace Engine
{

	namespace
	{

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

		std::unordered_set<entt::entity> visited;
		visited.reserve(dirtyEntities.size());

		for (entt::entity e : dirtyEntities)
		{
			if (e == entt::null || !visited.insert(e).second || !registry.valid(e) || !registry.any_of<Transform>(e))
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
			leaf.aabb = worldAABB;
			leaf.hasCullHistory = false;

			if (!AABBInsideAABB(leaf.aabb, leaf.fatAABB))
			{
				leaf.fatAABB = MakeFatAABB(leaf.aabb);
				anyLeafEscapedFatBounds = true;
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
			Refit(root);

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

		// 1. Compute bounding box & split axis
		AABB centroidBox;
		centroidBox.min = glm::vec3(FLT_MAX);
		centroidBox.max = glm::vec3(-FLT_MAX);

		for (int i = begin; i < end; ++i)
		{
			const AABB& aabb = nodes[leafIndices[i]].aabb;
			glm::vec3   ctr = 0.5f * (aabb.min + aabb.max);
			centroidBox.min = glm::min(centroidBox.min, ctr);
			centroidBox.max = glm::max(centroidBox.max, ctr);
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

		// 2. Partition around median
		int mid = begin + count / 2;
		std::nth_element(leafIndices.begin() + begin,
			leafIndices.begin() + mid,
			leafIndices.begin() + end,
			[&](int a, int b)
		{
			float ca = 0.5f * (nodes[a].aabb.min[axis] + nodes[a].aabb.max[axis]);
			float cb = 0.5f * (nodes[b].aabb.min[axis] + nodes[b].aabb.max[axis]);
			return ca < cb;
		});

		// 3. Create internal node 
		BVHNode internal;
		internal.left = BuildRecursive(leafIndices, begin, mid);
		internal.right = BuildRecursive(leafIndices, mid, end);

		// Enclose children
		const AABB& lTight = nodes[internal.left].aabb;
		const AABB& rTight = nodes[internal.right].aabb;
		internal.aabb = MergeAABBs(lTight, rTight);

		const AABB& lFat = nodes[internal.left].fatAABB;
		const AABB& rFat = nodes[internal.right].fatAABB;
		internal.fatAABB = MergeAABBs(lFat, rFat);

		int idx = static_cast<int>(nodes.size());
		nodes.emplace_back(std::move(internal));
		return idx;
	}

	void SceneBVH::FullRebuild()
	{
		std::vector<std::pair<entt::entity, int>> oldLeaves(entityToLeaf.begin(), entityToLeaf.end());

		nodes.clear();
		entityToLeaf.clear();

		std::vector<int> leafIndices;
		leafIndices.reserve(oldLeaves.size());

		// === Rebuild for Material entities ===
		auto view = registry.view<Transform, Material>();
		for (entt::entity e : view)
		{
			const auto& tf = view.get<Transform>(e);
			if (tf.GetTransformSpace() != TransformSpace::World) { continue; }

			const auto& mat = view.get<Material>(e).data;
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
		auto compositeView = registry.view<Transform, CompositeMaterial>();
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

		// Optional: sort for spatial locality
		std::sort(leafIndices.begin(), leafIndices.end());

		root = BuildRecursive(leafIndices, 0, static_cast<int>(leafIndices.size()));
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
