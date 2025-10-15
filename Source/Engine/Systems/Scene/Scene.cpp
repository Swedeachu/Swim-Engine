#include "PCH.h"
#include "Scene.h"
#include "Engine/Systems/Renderer/Vulkan/VulkanRenderer.h"
#include "Engine/Systems/Renderer/OpenGL/OpenGLRenderer.h"
#include "Engine/Components/Material.h"
#include "Engine/Components/Transform.h"
#include "Engine/Components/Internal/FrustumCullCache.h"
#include "Engine/Systems/Entity/EntityFactory.h"

namespace Engine
{

#ifdef _DEBUG
	constexpr static bool handleDebugDraw = true;
#else
	constexpr static bool handleDebugDraw = false;
#endif

	entt::entity Scene::CreateEntity()
	{
		return registry.create();
	}

	void Scene::DestroyEntity(entt::entity entity, bool callExit, bool destroyChildren)
	{
		if (!registry.valid(entity))
		{
			return;
		}

		// If it has a Transform, handle children and unlink from parent
		if (registry.any_of<Transform>(entity))
		{
			auto& tf = registry.get<Transform>(entity);

			// Handle children
			// Copy the list; it will be mutated
			std::vector<entt::entity> kids = tf.children;

			if (destroyChildren)
			{
				// Depth-first destroy of subtree
				for (auto child : kids)
				{
					DestroyEntity(child, callExit, true);
				}
			}
			else
			{
				// Detach children (null their parents)
				for (auto child : kids)
				{
					if (!registry.valid(child) || !registry.any_of<Transform>(child))
					{
						continue;
					}
					auto& ctf = registry.get<Transform>(child);
					// remove child from our list will happen after loop anyway
					ctf.parent = entt::null;
					ctf.MarkWorldDirtyOnly();
				}

				tf.children.clear();
			}

			// Unlink from our parent
			if (tf.parent != entt::null && registry.valid(tf.parent) && registry.any_of<Transform>(tf.parent))
			{
				auto& ptf = registry.get<Transform>(tf.parent);
				auto& vec = ptf.children;
				vec.erase(std::remove(vec.begin(), vec.end(), entity), vec.end());
			}

			tf.parent = entt::null;
		}

		// Call Exit() on behaviors if needed
		if (callExit && registry.any_of<BehaviorComponents>(entity))
		{
			auto& bc = registry.get<BehaviorComponents>(entity);
			for (auto& b : bc.behaviors)
			{
				if (b) b->Exit();
			}
		}

		// Finally destroy the entity itself
		registry.destroy(entity);
	}

	void Scene::DestroyAllEntities(bool callExit)
	{
		std::vector<entt::entity> toKill;

		for (auto entity : registry.storage<entt::entity>())
		{
			toKill.push_back(entity);
		}

		for (auto e : toKill)
		{
			if (!registry.valid(e))
			{
				continue;
			}

			bool hasTf = registry.any_of<Transform>(e);
			bool hasParent = false;

			if (hasTf)
			{
				hasParent = registry.get<Transform>(e).parent != entt::null;
			}

			if (!hasParent)
			{
				DestroyEntity(e, callExit, true);
			}
		}

		for (auto e : toKill)
		{
			if (registry.valid(e))
			{
				DestroyEntity(e, callExit, true);
			}
		}
	}

	void Scene::SetParent(entt::entity child, entt::entity parent)
	{
		// Avoid self-parenting
		if (child == parent)
		{
			return;
		}

		// Safety
		if (!registry.valid(child) || !registry.any_of<Transform>(child))
		{
			return;
		}

		// Allow nulling by passing entt::null via RemoveParent instead.
		if (!registry.valid(parent) || !registry.any_of<Transform>(parent))
		{
			return;
		}

		// Avoid cycles
		if (WouldCreateCycle(registry, child, parent))
		{
			return;
		}

		auto& childTf = registry.get<Transform>(child);

		// If already same parent, nothing to do
		if (childTf.parent == parent)
		{
			return;
		}

		// Remove from old parent's children list
		if (childTf.parent != entt::null && registry.valid(childTf.parent) && registry.any_of<Transform>(childTf.parent))
		{
			auto& oldParentTf = registry.get<Transform>(childTf.parent);
			auto& vec = oldParentTf.children;
			vec.erase(std::remove(vec.begin(), vec.end(), child), vec.end());
		}

		// Set new parent + register child
		childTf.parent = parent;
		auto& parentTf = registry.get<Transform>(parent);
		parentTf.children.push_back(child);

		// Invalidate child's world and all its descendants (lazy recompute on demand)
		std::vector<entt::entity> stack;
		stack.push_back(child);

		while (!stack.empty())
		{
			entt::entity e = stack.back();
			stack.pop_back();

			if (!registry.valid(e) || !registry.any_of<Transform>(e))
			{
				continue;
			}

			auto& tf = registry.get<Transform>(e);
			tf.MarkWorldDirtyOnly();

			for (auto c : tf.children)
			{
				stack.push_back(c);
			}
		}
	}

	void Scene::RemoveParent(entt::entity child)
	{
		if (!registry.valid(child) || !registry.any_of<Transform>(child))
		{
			return;
		}

		auto& childTf = registry.get<Transform>(child);

		// Remove from old parent's children list
		if (childTf.parent != entt::null && registry.valid(childTf.parent) && registry.any_of<Transform>(childTf.parent))
		{
			auto& oldParentTf = registry.get<Transform>(childTf.parent);
			auto& vec = oldParentTf.children;
			vec.erase(std::remove(vec.begin(), vec.end(), child), vec.end());
		}

		// Clear parent
		childTf.parent = entt::null;

		// Invalidate subtree world matrices
		std::vector<entt::entity> stack;
		stack.push_back(child);

		while (!stack.empty())
		{
			entt::entity e = stack.back();
			stack.pop_back();

			if (!registry.valid(e) || !registry.any_of<Transform>(e))
			{
				continue;
			}

			auto& tf = registry.get<Transform>(e);
			tf.MarkWorldDirtyOnly();

			for (auto c : tf.children)
			{
				stack.push_back(c);
			}
		}
	}

	std::vector<entt::entity>* Scene::GetChildren(entt::entity e)
	{
		if (registry.valid(e) && registry.any_of<Transform>(e))
		{
			Transform& tf = registry.get<Transform>(e);
			return &tf.children;
		}

		return nullptr;
	}

	entt::entity Scene::GetParent(entt::entity e) const
	{
		if (registry.valid(e) && registry.any_of<Transform>(e))
		{
			const auto& tf = registry.get<Transform>(e);
			return tf.parent;
		}

		return entt::null;
	}

	bool Scene::WouldCreateCycle(const entt::registry& reg, entt::entity child, entt::entity newParent)
	{
		// climb from newParent up to root; if we see child, that's a cycle
		entt::entity cur = newParent;

		while (cur != entt::null && reg.valid(cur) && reg.any_of<Transform>(cur))
		{
			if (cur == child)
			{
				return true;
			}

			const auto& tf = reg.get<Transform>(cur);

			cur = tf.parent;
		}

		return false;
	}

	void Scene::SetVulkanRenderer(const std::shared_ptr<VulkanRenderer>& system)
	{
		vulkanRenderer = system;
		renderer = std::static_pointer_cast<Renderer>(system);
	}

	void Scene::SetOpenGLRenderer(const std::shared_ptr<OpenGLRenderer>& system)
	{
		openGLRenderer = system;
		renderer = std::static_pointer_cast<Renderer>(system);
	}

	std::shared_ptr<Renderer> Scene::GetRenderer() const
	{
		return GetSystem<Renderer>(renderer);
	}

	// Right now the interal scene base init and update are for caching mesh stuff for the frustum culling.
	// Sooner or later we will have more code here for full on spacial partioning of the scene, 
	// which will be essential for physics and AI and rendering/generic updates of active chunks.
	// We are already doing that now with SceneBVH.

	void Scene::InternalSceneAwake()
	{
		ForEachBehavior(&Behavior::Awake); // we might not want to do this actually and let behaviors do this themselves
	}

	void Scene::InternalSceneInit()
	{
		// Watch for updates (construction or modification) of Transform or Material
		frustumCacheObserver.connect(registry, entt::collector
			.group<Engine::Transform, Engine::Material>()
		);

		// Auto-remove FrustumCullCache when prerequisites are destroyed
		registry.on_destroy<Engine::Transform>().connect<&Scene::RemoveFrustumCache>(*this);
		registry.on_destroy<Engine::Material>().connect<&Scene::RemoveFrustumCache>(*this);

		// Initialize SceneBVH grid
		sceneBVH = std::make_unique<SceneBVH>(registry);
		sceneBVH->Init();

		// Initialize the debug drawer
		sceneDebugDraw = std::make_unique<SceneDebugDraw>();
		sceneDebugDraw->Init();

		sceneBVH->SetDebugDrawer(sceneDebugDraw.get());

		gizmoSystem = std::make_unique<GizmoSystem>();
		std::shared_ptr<Scene> self = shared_from_this();
		gizmoSystem->SetScene(self);
		gizmoSystem->Awake();
		gizmoSystem->Init();

		ForEachBehavior(&Behavior::Init); // we might not want to do this actually and let behaviors do this themselves
	}

	void Scene::RemoveFrustumCache(entt::registry& registry, entt::entity entity)
	{
		if (registry.any_of<Engine::FrustumCullCache>(entity))
		{
			registry.remove<Engine::FrustumCullCache>(entity);
		}
	}

	// Stuff we don't need to happen thousands of times a second, or needs to be timed, such as physics scene updates.
	// It might make sense to have sub scene systems be in a data structure that iterates with update inside of here and init and update etc.
	void Scene::InternalFixedUpdate(unsigned int tickThisSecond)
	{
		if (gizmoSystem)
		{
			gizmoSystem->FixedUpdate(tickThisSecond);
		}

		// Call fixed update on all our behaviors
		ForEachBehavior(&Behavior::FixedUpdate, tickThisSecond);

		// Add new frustum cache components if needed
		for (auto entity : frustumCacheObserver)
		{
			if (!registry.any_of<Engine::FrustumCullCache>(entity))
			{
				registry.emplace<Engine::FrustumCullCache>(entity);
			}
		}

		// Let BVH manage itself
		if (sceneBVH)
		{
			sceneBVH->UpdateIfNeeded(frustumCacheObserver);
		}

		frustumCacheObserver.clear();
	}

	void Scene::InternalScenePostUpdate(double dt)
	{
		Transform::ClearGlobalDirtyFlag();

		// if constexpr (handleDebugDraw)
		sceneBVH->DebugRender();
	}

	void Scene::InternalSceneExit()
	{
		// TODO: don't destroy on load/persist entities 
		ForEachBehavior(&Behavior::Exit);
	}

	void Scene::InternalSceneUpdate(double dt)
	{
		// Clear the previous frames debug draw data. 
		// This opens up an opportunity for caching commonly drawn wireframes.
		// sceneDebugDraw->Clear();

		// We want to keep editor mode objects such as retained gizmos, trash everything else that is immediate mode from the previous frame
		constexpr static std::array<int, 1> keep = { TagConstants::EDITOR_MODE_OBJECT };
		sceneDebugDraw->ClearExceptTags(keep);

		EntityFactory& entityFactory = EntityFactory::GetInstance();
		entityFactory.ProcessQueues(); // Start of a new frame, handle all the new created and deleted entities from the previous frame here.

		// Ensure BVH is coherent for this frame if any entity was removed/added or forced.
		if (sceneBVH && sceneBVH->ShouldForceUpdate())
		{
			sceneBVH->Update();
		}

		// Call Update(dt) on all Behavior components.
		ForEachBehavior(&Behavior::Update, dt);
		UpdateUIBehaviors(); 

		if (gizmoSystem)
		{
			gizmoSystem->Update(dt);
		}

		// Was doing bvh update here but its more performant to do it in the fixed update.

		// if constexpr (handleDebugDraw)
		{
			auto input = GetInputManager();
			// control toggle with G key 
			if (input->IsKeyDown(VK_CONTROL) && input->IsKeyTriggered('G'))
			{
				sceneDebugDraw->SetEnabled(!sceneDebugDraw->IsEnabled());
				std::string abled = sceneDebugDraw->IsEnabled() ? "Enabled" : "Disabled";
				std::cout << "Debug wireframe draw " << abled << "\n";
			}
		}
	}

	// Converts the mouse position from window-pixel space -> virtual-canvas space
	// Tests that virtual point against each screen-space entity’s AABB
	// Dispatches OnMouseEnter / Exit / Hover / Click events
	void Scene::UpdateUIBehaviors()
	{
		mouseBusyWithUI = false; // reset mouse pointer UI focus status for this frame

		// 1. Get raw mouse position in window pixels
		std::shared_ptr<InputManager> inputMgr = GetInputManager();
		glm::vec2 mouseVirt = inputMgr->GetMousePosition(true);

		// 2. Iterate over UI entities and run hit-testing in the same space
		auto& registry = GetRegistry();

		registry.view<Transform, Material, BehaviorComponents>().each(
			[&](entt::entity entity,
			Transform& transform,
			Material&, BehaviorComponents& bc)
		{
			if (transform.GetTransformSpace() != TransformSpace::Screen)
			{
				return; // ignore world-space stuff here
			}

			// World position (center of quad in virtual-canvas units)
			glm::vec3 pos = transform.GetWorldPosition(registry); // xyz from translation column

			// World scale = lengths of basis vectors (handles non-uniform scale).
			// For UI AABB we only care about X/Y; sign doesn't matter for extents.
			glm::vec3 scl = transform.GetWorldScale(registry);

			// Position / size are now in world (screen) virtual-canvas units
			glm::vec2 halfSize{ 0.5f * std::abs(scl.x), 0.5f * std::abs(scl.y) };

			glm::vec2 minRect{ pos.x - halfSize.x, pos.y - halfSize.y };
			glm::vec2 maxRect{ pos.x + halfSize.x, pos.y + halfSize.y };

			bool inside = (mouseVirt.x >= minRect.x && mouseVirt.x <= maxRect.x
				&& mouseVirt.y >= minRect.y && mouseVirt.y <= maxRect.y);

			// 3. Let each attached behaviour react
			for (auto& behavior : bc.behaviors)
			{
				if (!behavior || !behavior->RunMouseCallBacks())
				{
					continue;
				}

				bool wasFocused = behavior->FocusedByMouse();

				if (inside && !wasFocused) // mouse first enter
				{
					mouseBusyWithUI = true;
					behavior->SetFocusedByMouse(true);
					behavior->OnMouseEnter();
				}
				else if (!inside && wasFocused) // mouse exit
				{
					behavior->SetFocusedByMouse(false);
					behavior->OnMouseExit();
				}
				else if (inside) // mouse hover + possible focused input interactions from mouse clicking
				{
					mouseBusyWithUI = true;
					behavior->OnMouseHover();

					if (inputMgr->IsKeyDown(VK_LBUTTON)) { behavior->OnLeftClickDown(); }
					if (inputMgr->IsKeyDown(VK_RBUTTON)) { behavior->OnRightClickDown(); }

					if (inputMgr->IsKeyReleased(VK_LBUTTON)) { behavior->OnLeftClickUp(); }
					if (inputMgr->IsKeyReleased(VK_RBUTTON)) { behavior->OnRightClickUp(); }

					if (inputMgr->IsKeyTriggered(VK_LBUTTON)) { behavior->OnLeftClicked(); }
					if (inputMgr->IsKeyTriggered(VK_RBUTTON)) { behavior->OnRightClicked(); }
				}
			}
		});
	}

	// Point is in screen pixels, (0,0) = top-left.
	Ray Scene::ScreenPointToRay(const glm::vec2& point) const
	{
		Camera& cam = GetCameraSystem()->GetCamera();

		std::shared_ptr<SwimEngine> engine = SwimEngine::GetInstance();
		const float width = static_cast<float>(engine->GetWindowWidth());
		const float height = static_cast<float>(engine->GetWindowHeight());

		// top-left-origin pixels -> NDC
		float ndcX = (2.0f * point.x) / width - 1.0f;  // [-1,+1], left->right
		float ndcY = 1.0f - (2.0f * point.y) / height;  // [-1,+1], top->bottom

		// Camera params
		const float fovY = glm::radians(cam.GetFOV());
		const float tanHalfFovY = tanf(fovY * 0.5f);
		float aspect = cam.GetAspect();
		if (aspect <= 0.0f && height > 0.0f) aspect = width / height;
		const float zNear = cam.GetNearClip();

		// View-space direction (RH, forward = -Z)
		const glm::vec3 dirVS(ndcX * tanHalfFovY * aspect, ndcY * tanHalfFovY, -1.0f);

		// Point on the near plane for this screen pixel (z = -zNear in view space)
		const glm::vec3 nearVS = dirVS * (zNear / -dirVS.z);

		// Rotate into world space & build ray
		const glm::quat q = cam.GetRotation();
		const glm::vec3 origin = cam.GetPosition() + (q * nearVS);
		const glm::vec3 dir = glm::normalize(q * dirVS);

		return Ray(origin, dir);
	}

	ObjectTag* Scene::GetTag(entt::entity entity)
	{
		if (registry.valid(entity) && registry.any_of<ObjectTag>(entity))
		{
			return &registry.get<ObjectTag>(entity);
		}

		return nullptr;
	}

	void Scene::SetTag(entt::entity entity, int tag, const std::string& name)
	{
		if (registry.valid(entity))
		{
			if (registry.any_of<ObjectTag>(entity))
			{
				auto& t = registry.get<ObjectTag>(entity);
				t.tag = tag;
				t.name = name;
			}
			else
			{
				registry.emplace<ObjectTag>(entity, tag, name);
			}
		}
	}

	void Scene::RemoveTag(entt::entity entity)
	{
		if (registry.valid(entity) && registry.any_of<ObjectTag>(entity))
		{
			registry.remove<ObjectTag>(entity);
		}
	}

	void Scene::InternalFixedPostUpdate(unsigned int tickThisSecond)
	{
		// nothing to do here
	}

}
