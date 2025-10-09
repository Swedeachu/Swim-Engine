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

	void Scene::DestroyEntity(entt::entity entity, bool callExit)
	{
		if (callExit)
		{
			ForEachBehaviorOfEntity(entity, &Behavior::Exit);
		}

		registry.destroy(entity);
	}

	void Scene::DestroyAllEntities(bool callExit)
	{
		if (callExit)
		{
			ForEachBehavior(&Behavior::Exit);
		}

		registry.clear();
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
		sceneDebugDraw->Clear();

		EntityFactory& entityFactory = EntityFactory::GetInstance();
		entityFactory.ProcessQueues(); // Start of a new frame, handle all the new created and deleted entities from the previous frame here.

		// Call Update(dt) on all Behavior components
		ForEachBehavior(&Behavior::Update, dt);
		UpdateUIBehaviors();

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
		// 1. Get raw mouse position in window pixels
		std::shared_ptr<InputManager> inputMgr = GetInputManager();
		glm::vec2 mouseWin = inputMgr->GetMousePosition(); // (0,0) = window TL

		// 2. Convert that to virtual canvas units (same space as Transform)
		auto engine = Engine::SwimEngine::GetInstance();

		float windowW = static_cast<float>(engine->GetWindowWidth());
		float windowH = static_cast<float>(engine->GetWindowHeight());

		constexpr float virtW = Engine::Renderer::VirtualCanvasWidth;
		constexpr float virtH = Engine::Renderer::VirtualCanvasHeight;

		float scaleX = windowW / virtW;
		float scaleY = windowH / virtH;

		// window top border hack fix
		float offset = 14; // was 28
		if (scaleY > 0.9f)
		{
			offset = 0.f;
		}

		glm::vec2 mouseVirt;
		mouseVirt.x = mouseWin.x / scaleX; // undo X scale
		mouseVirt.y = mouseWin.y / scaleY; // undo Y scale
		mouseVirt.y = virtH - mouseVirt.y - offset; // flip origin TL -> BL

		// 3. Iterate over UI entities and run hit-testing in the same space
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

			// Position / size are already in virtual-canvas units
			glm::vec3 pos = transform.GetPosition(); // center of quad
			glm::vec3 size = transform.GetScale(); // full width / height

			glm::vec2 halfSize{ size.x * 0.5f, size.y * 0.5f };

			glm::vec2 minRect{ pos.x - halfSize.x, pos.y - halfSize.y };
			glm::vec2 maxRect{ pos.x + halfSize.x, pos.y + halfSize.y };

			bool inside = (mouseVirt.x >= minRect.x && mouseVirt.x <= maxRect.x && mouseVirt.y >= minRect.y && mouseVirt.y <= maxRect.y);

			// 4. Let each attached behaviour react
			for (auto& behavior : bc.behaviors)
			{
				if (!behavior || !behavior->RunMouseCallBacks())
				{
					continue;
				}

				bool wasFocused = behavior->FocusedByMouse();

				if (inside && !wasFocused)
				{
					behavior->SetFocusedByMouse(true);
					behavior->OnMouseEnter();
				}
				else if (!inside && wasFocused)
				{
					behavior->SetFocusedByMouse(false);
					behavior->OnMouseExit();
				}
				else if (inside) // hover
				{
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

	// Point is in screen pixels, (0,0) = top-left. Need to investigate if the position is off by a tiny bit.
	Ray Scene::ScreenPointToRay(const glm::vec2& point) const
	{
		Camera& cam = GetCameraSystem()->GetCamera();

		// Use your actual render viewport if it differs from the window size.
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

	void Scene::InternalFixedPostUpdate(unsigned int tickThisSecond)
	{
		// nothing to do here
	}

}
