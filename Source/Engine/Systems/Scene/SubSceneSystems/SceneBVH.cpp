#include "PCH.h"
#include "SceneBVH.h"
#include "Engine/Components/Material.h"
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

	SceneBVH::AABB SceneBVH::CalculateWorldAABB(const std::shared_ptr<Mesh>& mesh, const Transform& transform)
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
#undef EXPAND_CORNER

	void SceneBVH::Update()
	{
		// Refresh or create new leafs
		auto view = registry.view<Transform, Material>();
		for (entt::entity e : view)
		{
			const Transform& tf = view.get<Transform>(e);
			if (tf.GetTransformSpace() != TransformSpace::World) continue; // ignore stuff not in the world
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
				// Brand new leaf
				BVHNode leaf;
				leaf.entity = e;
				leaf.aabb = CalculateWorldAABB(mesh, tf);
				int newIdx = static_cast<int>(nodes.size());
				nodes.emplace_back(std::move(leaf));
				entityToLeaf[e] = newIdx;
			}
		}

		static constexpr float kRebuildThreshold = 0.15f;

		if (root == -1 || observer.size() > 0)
		{
			FullRebuild();
		}
		else
		{
			// Store old AABB
			AABB preAABB = nodes[root].aabb;

			Refit(root);

			// Calculate size change using surface area for more accuracy
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
		// Step 1: Gather all entities and clear current data
		std::vector<std::pair<entt::entity, int>> oldLeaves(entityToLeaf.begin(), entityToLeaf.end());

		// Step 2: Clear all nodes and reset entity map
		nodes.clear();
		entityToLeaf.clear();

		// Step 3: Rebuild leaves and update map
		std::vector<int> leafIndices;
		leafIndices.reserve(oldLeaves.size());

		for (auto& [entity, _] : oldLeaves)
		{
			auto& tf = registry.get<Transform>(entity);
			auto& mat = registry.get<Material>(entity);
			const auto& mesh = mat.data->mesh;

			BVHNode leaf;
			leaf.entity = entity;
			leaf.aabb = CalculateWorldAABB(mesh, tf);

			int idx = static_cast<int>(nodes.size());
			nodes.emplace_back(std::move(leaf));
			entityToLeaf[entity] = idx;
			leafIndices.push_back(idx);
		}

		// Step 4: Re-order leaf indices for cache coherence (technically optional)
		std::sort(leafIndices.begin(), leafIndices.end());

		// Step 5: Recursively build BVH tree from leaves
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
		bool drawInternal = false;

		if (debugDrawer == nullptr || !debugDrawer->IsEnabled())
		{
			return;
		}

		const size_t start = drawInternal ? 0 : entityToLeaf.size();
		for (size_t i = start; i < nodes.size(); ++i)
		{
			const AABB& a = nodes[i].aabb;
			debugDrawer->SubmitWireframeBoxAABB(a.min, a.max);
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
