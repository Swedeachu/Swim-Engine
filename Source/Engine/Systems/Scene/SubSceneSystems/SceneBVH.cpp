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
		// Gather all leaf indices (they live at the front of nodes[])
		std::vector<int> leafIndices;
		leafIndices.reserve(entityToLeaf.size());
		for (auto [e, idx] : entityToLeaf)
		{
			leafIndices.push_back(idx);
		}

		// Re-order leaves contiguously for cache coherence
		std::sort(leafIndices.begin(), leafIndices.end());

		// Start building: we append internal nodes at the back
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

	// TODO: write another version of this that takes a callback function and runs the code immediately on the entity if it is not culled, 
	// instead of needing to populate a vector to then do stuff with.
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

#define TEST_CORNER(plane, x, y, z) \
    (glm::dot(glm::vec3(plane), glm::vec3(x, y, z)) + (plane).w < 0.0f ? 1 : 0)

	bool SceneBVH::IsAABBVisible(const Frustum& frustum, const AABB& aabb) const
	{
		for (int i = 0; i < 6; ++i)
		{
			const glm::vec4& plane = frustum.planes[i];
			int outside = 0;

			outside += TEST_CORNER(plane, aabb.min.x, aabb.min.y, aabb.min.z);
			outside += TEST_CORNER(plane, aabb.max.x, aabb.min.y, aabb.min.z);
			outside += TEST_CORNER(plane, aabb.min.x, aabb.max.y, aabb.min.z);
			outside += TEST_CORNER(plane, aabb.max.x, aabb.max.y, aabb.min.z);
			outside += TEST_CORNER(plane, aabb.min.x, aabb.min.y, aabb.max.z);
			outside += TEST_CORNER(plane, aabb.max.x, aabb.min.y, aabb.max.z);
			outside += TEST_CORNER(plane, aabb.min.x, aabb.max.y, aabb.max.z);
			outside += TEST_CORNER(plane, aabb.max.x, aabb.max.y, aabb.max.z);

			if (outside == 8)
			{
				return false;
			}
		}
		return true;
	}
#undef TEST_CORNER

}
