#include "PCH.h"
#include "SceneBVH.h"
#include "Engine/Components/Material.h"
#include "Engine/Components/CompositeMaterial.h"
#include "Engine/Components/Transform.h"
#include "Engine/Systems/Renderer/Core/Meshes/Mesh.h"
#include "Engine/Systems/Renderer/Core/Camera/Frustum.h"

namespace Engine
{

	SceneBVH::SceneBVH(entt::registry& registry)
		: registry{ registry }
	{}

	void SceneBVH::Init()
	{
		observer.connect(registry, entt::collector
			.group<Transform, Material>()
			.update<Transform>()
			.update<Material>());

		registry.on_destroy<Transform>().connect<&SceneBVH::RemoveEntity>(*this);
		registry.on_destroy<Material>().connect<&SceneBVH::RemoveEntity>(*this);
	}

#define EXPAND_CORNER(x, y, z)                                            \
{                                                                         \
    glm::vec3 c = glm::vec3(model * glm::vec4(x, y, z, 1.0f));            \
    worldMin = glm::min(worldMin, c);                                     \
    worldMax = glm::max(worldMax, c);                                     \
}

	AABB SceneBVH::CalculateWorldAABB(const std::shared_ptr<Mesh>& mesh, const Transform& transform)
	{
		const glm::vec3& localMin = mesh->meshBufferData->aabbMin;
		const glm::vec3& localMax = mesh->meshBufferData->aabbMax;
		glm::mat4 model = transform.GetModelMatrix();

		glm::vec3 worldMin = glm::vec3(model * glm::vec4(localMin, 1.0f));
		glm::vec3 worldMax = worldMin;

		// EXPAND_CORNER(localMin.x, localMin.y, localMin.z);
		EXPAND_CORNER(localMax.x, localMin.y, localMin.z);
		EXPAND_CORNER(localMin.x, localMax.y, localMin.z);
		EXPAND_CORNER(localMax.x, localMax.y, localMin.z);
		EXPAND_CORNER(localMin.x, localMin.y, localMax.z);
		EXPAND_CORNER(localMax.x, localMin.y, localMax.z);
		EXPAND_CORNER(localMin.x, localMax.y, localMax.z);
		EXPAND_CORNER(localMax.x, localMax.y, localMax.z);

		return { worldMin, worldMax };
	}

	void SceneBVH::Update()
	{
		// === Handle regular Material entities ===
		auto view = registry.view<Transform, Material>();
		for (entt::entity e : view)
		{
			const Transform& tf = view.get<Transform>(e);
			if (tf.GetTransformSpace() != TransformSpace::World) { continue; }

			const std::shared_ptr<MaterialData>& mat = view.get<Material>(e).data;
			const std::shared_ptr<Mesh>& mesh = mat->mesh;

			const bool isDirty = tf.IsDirty();

			auto it = entityToLeaf.find(e);
			if (it != entityToLeaf.end())
			{
				if (isDirty)
				{
					nodes[it->second].aabb = CalculateWorldAABB(mesh, tf);
				}
			}
			else
			{
				BVHNode leaf;
				leaf.entity = e;
				leaf.aabb = CalculateWorldAABB(mesh, tf);
				int newIdx = static_cast<int>(nodes.size());
				nodes.emplace_back(std::move(leaf));
				entityToLeaf[e] = newIdx;
			}
		}

		// === Handle CompositeMaterial entities ===
		auto compositeView = registry.view<Transform, CompositeMaterial>();
		for (entt::entity e : compositeView)
		{
			const Transform& tf = compositeView.get<Transform>(e);
			if (tf.GetTransformSpace() != TransformSpace::World) { continue; }

			const CompositeMaterial& comp = compositeView.get<CompositeMaterial>(e);
			if (comp.subMaterials.empty()) { continue; }

			// Compute combined local AABB
			glm::vec3 localMin(FLT_MAX);
			glm::vec3 localMax(-FLT_MAX);

			for (const auto& mat : comp.subMaterials)
			{
				if (!mat || !mat->mesh || !mat->mesh->meshBufferData) { continue; }

				const glm::vec3& min = glm::vec3(mat->mesh->meshBufferData->aabbMin);
				const glm::vec3& max = glm::vec3(mat->mesh->meshBufferData->aabbMax);

				localMin = glm::min(localMin, min);
				localMax = glm::max(localMax, max);
			}

			// Transform to world space
			glm::mat4 model = tf.GetModelMatrix();

			glm::vec3 worldMin = glm::vec3(model * glm::vec4(localMin, 1.0f));
			glm::vec3 worldMax = worldMin;

			EXPAND_CORNER(localMax.x, localMin.y, localMin.z);
			EXPAND_CORNER(localMin.x, localMax.y, localMin.z);
			EXPAND_CORNER(localMax.x, localMax.y, localMin.z);
			EXPAND_CORNER(localMin.x, localMin.y, localMax.z);
			EXPAND_CORNER(localMax.x, localMin.y, localMax.z);
			EXPAND_CORNER(localMin.x, localMax.y, localMax.z);
			EXPAND_CORNER(localMax.x, localMax.y, localMax.z);

			AABB worldAABB = { worldMin, worldMax };

			const bool isDirty = tf.IsDirty();

			auto it = entityToLeaf.find(e);
			if (it != entityToLeaf.end())
			{
				if (isDirty)
				{
					nodes[it->second].aabb = worldAABB;
				}
			}
			else
			{
				BVHNode leaf;
				leaf.entity = e;
				leaf.aabb = worldAABB;
				int newIdx = static_cast<int>(nodes.size());
				nodes.emplace_back(std::move(leaf));
				entityToLeaf[e] = newIdx;
			}
		}

		// === Rebuild check ===
		static constexpr float kRebuildThreshold = 0.15f;

		if (root == -1 || observer.size() > 0)
		{
			FullRebuild();
		}
		else
		{
			AABB preAABB = nodes[root].aabb;

			Refit(root);

			glm::vec3 preSize = preAABB.max - preAABB.min;
			glm::vec3 postSize = nodes[root].aabb.max - nodes[root].aabb.min;

			float preArea = 2.0f * (preSize.x * preSize.y + preSize.x * preSize.z + preSize.y * preSize.z);
			float postArea = 2.0f * (postSize.x * postSize.y + postSize.x * postSize.z + postSize.y * postSize.z);

			if (preArea > 0.0f)
			{
				float expansion = postArea / preArea;
				if (expansion > 1.0f + kRebuildThreshold)
				{
					FullRebuild();
				}
			}
		}

		observer.clear();
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
		const AABB& l = nodes[internal.left].aabb;
		const AABB& r = nodes[internal.right].aabb;
		internal.aabb.min = glm::min(l.min, r.min);
		internal.aabb.max = glm::max(l.max, r.max);

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

			glm::vec3 localMin(FLT_MAX);
			glm::vec3 localMax(-FLT_MAX);

			for (const auto& mat : comp.subMaterials)
			{
				if (!mat || !mat->mesh || !mat->mesh->meshBufferData) { continue; }

				const glm::vec3& min = glm::vec3(mat->mesh->meshBufferData->aabbMin);
				const glm::vec3& max = glm::vec3(mat->mesh->meshBufferData->aabbMax);

				localMin = glm::min(localMin, min);
				localMax = glm::max(localMax, max);
			}

			glm::mat4 model = tf.GetModelMatrix();
			glm::vec3 worldMin = glm::vec3(model * glm::vec4(localMin, 1.0f));
			glm::vec3 worldMax = worldMin;

			EXPAND_CORNER(localMax.x, localMin.y, localMin.z);
			EXPAND_CORNER(localMin.x, localMax.y, localMin.z);
			EXPAND_CORNER(localMax.x, localMax.y, localMin.z);
			EXPAND_CORNER(localMin.x, localMin.y, localMax.z);
			EXPAND_CORNER(localMax.x, localMin.y, localMax.z);
			EXPAND_CORNER(localMin.x, localMax.y, localMax.z);
			EXPAND_CORNER(localMax.x, localMax.y, localMax.z);

			AABB worldAABB = { worldMin, worldMax };

			BVHNode leaf;
			leaf.entity = e;
			leaf.aabb = worldAABB;

			int idx = static_cast<int>(nodes.size());
			nodes.emplace_back(std::move(leaf));
			entityToLeaf[e] = idx;
			leafIndices.push_back(idx);
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

		const AABB& l = nodes[node.left].aabb;
		const AABB& r = nodes[node.right].aabb;
		node.aabb.min = glm::min(l.min, r.min);
		node.aabb.max = glm::max(l.max, r.max);
	}

	inline void SceneBVH::PushIfVisible(int nodeIdx, const Frustum& frustum, std::vector<int>& stack) const
	{
		if (IsAABBVisible(frustum, nodes[nodeIdx].aabb))
		{
			stack.push_back(nodeIdx);
		}
	}

	void SceneBVH::QueryFrustum(const Frustum& frustum, std::vector<entt::entity>& outVisible) const
	{
		outVisible.clear();
		if (root == -1)
		{
			return;
		}

		std::vector<int> stack;
		stack.reserve(128); // was 32
		stack.push_back(root);

		while (!stack.empty())
		{
			int idx = stack.back();
			stack.pop_back();

			const BVHNode& n = nodes[idx];
			if (n.IsLeaf())
			{
				outVisible.push_back(n.entity);
			}
			else
			{
				PushIfVisible(n.left, frustum, stack);
				PushIfVisible(n.right, frustum, stack);
			}
		}

	#ifdef _DEBUG
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

				const AABB& a = node.aabb;
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

					const glm::mat4 model = tf.GetModelMatrix();

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
		bool needsUpdate = frustumObserver.empty();

		if (!needsUpdate && Transform::AreAnyTransformsDirty())
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

		int idx = it->second;
		int last = static_cast<int>(nodes.size()) - 1;

		if (idx != last)
		{
			std::swap(nodes[idx], nodes[last]);

			// Update bookkeeping for the swapped leaf (could be internal or leaf)
			if (nodes[idx].IsLeaf())
			{
				entityToLeaf[nodes[idx].entity] = idx;
			}
			else
			{
				// internal node swapped into leaf slot: children remain correct
			}
		}

		nodes.pop_back();
		entityToLeaf.erase(it);

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
		if (!RayIntersectsAABB(ray, nodes[root].aabb, tMin, tMax, tRoot))
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
				// Use carried tnear as the leaf hit distance (no re-test)
				const float tLeaf = it.tnear;
				if (tLeaf >= tMin && tLeaf <= bestT)
				{
					bestT = tLeaf;
					bestE = node.entity;
				}
				continue;
			}

			// Intersect children with tMax tightened to current best
			float tL, tR;
			const bool hitL = RayIntersectsAABB(ray, nodes[node.left].aabb, tMin, bestT, tL);
			const bool hitR = RayIntersectsAABB(ray, nodes[node.right].aabb, tMin, bestT, tR);

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

	/* classic
	bool SceneBVH::IsAABBVisible(const Frustum& frustum, const AABB& aabb) const
	{
		// Test against all 6 frustum planes
		for (int i = 0; i < 6; ++i)
		{
			const glm::vec4& plane = frustum.planes[i];

			// Assume box is completely outside this plane
			bool allOutside = true;

			// Test all 8 corners; if any corner is inside, box is not fully outside
			if ((glm::dot(glm::vec3(plane), glm::vec3(aabb.min.x, aabb.min.y, aabb.min.z)) + plane.w) >= 0.0f) { allOutside = false; }
			if ((glm::dot(glm::vec3(plane), glm::vec3(aabb.max.x, aabb.min.y, aabb.min.z)) + plane.w) >= 0.0f) { allOutside = false; }
			if ((glm::dot(glm::vec3(plane), glm::vec3(aabb.min.x, aabb.max.y, aabb.min.z)) + plane.w) >= 0.0f) { allOutside = false; }
			if ((glm::dot(glm::vec3(plane), glm::vec3(aabb.max.x, aabb.max.y, aabb.min.z)) + plane.w) >= 0.0f) { allOutside = false; }
			if ((glm::dot(glm::vec3(plane), glm::vec3(aabb.min.x, aabb.min.y, aabb.max.z)) + plane.w) >= 0.0f) { allOutside = false; }
			if ((glm::dot(glm::vec3(plane), glm::vec3(aabb.max.x, aabb.min.y, aabb.max.z)) + plane.w) >= 0.0f) { allOutside = false; }
			if ((glm::dot(glm::vec3(plane), glm::vec3(aabb.min.x, aabb.max.y, aabb.max.z)) + plane.w) >= 0.0f) { allOutside = false; }
			if ((glm::dot(glm::vec3(plane), glm::vec3(aabb.max.x, aabb.max.y, aabb.max.z)) + plane.w) >= 0.0f) { allOutside = false; }

			// If all corners are outside any plane, box is outside frustum
			if (allOutside)
			{
				return false;
			}
		}

		// Box is inside or intersects frustum
		return true;
	}
	*/

	/*
	bool SceneBVH::IsAABBVisible(const Frustum& frustum, const AABB& aabb) const
	{
		// For each plane of the frustum
		for (int i = 0; i < 6; ++i)
		{
			const glm::vec4& plane = frustum.planes[i];

			// Test if all 8 corners are outside this plane
			bool allOutside = true;

			// Corners of AABB
			const float x[2] = { aabb.min.x, aabb.max.x };
			const float y[2] = { aabb.min.y, aabb.max.y };
			const float z[2] = { aabb.min.z, aabb.max.z };

			// Check all combinations (2x2x2 = 8 corners)
			for (int ix = 0; ix < 2; ++ix)
			{
				for (int iy = 0; iy < 2; ++iy)
				{
					for (int iz = 0; iz < 2; ++iz)
					{
						glm::vec3 corner(x[ix], y[iy], z[iz]);
						float distance = glm::dot(glm::vec3(plane), corner) + plane.w;

						if (distance >= 0.0f)
						{
							allOutside = false;
							goto NextPlane; // Early-exit to next plane
						}
					}
				}
			}

			// If all corners are outside this plane, box is not visible
			return false;

NextPlane:;
		}

		// Box is at least partially inside frustum
		return true;
	}
	//*/

	//* manual slab method seems fastest right now
	bool SceneBVH::IsAABBVisible(const Frustum& frustum, const AABB& aabb) const
	{
		for (int i = 0; i < 6; ++i)
		{
			const glm::vec4& plane = frustum.planes[i];

			// Compute dot product manually (the ternarys might hurt branch prediction, not sure)
			if (
				(
				plane.x * ((plane.x >= 0.0f) ? aabb.max.x : aabb.min.x)
				+ plane.y * ((plane.y >= 0.0f) ? aabb.max.y : aabb.min.y)
				+ plane.z * ((plane.z >= 0.0f) ? aabb.max.z : aabb.min.z)
				)
				+ plane.w < 0.0f
				)
			{
				return false;
			}
		}

		return true;
	}
	//*/

}
