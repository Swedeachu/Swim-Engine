#include "PCH.h"
#include "Scene.h"
#include "Engine/Systems/Renderer/Vulkan/VulkanRenderer.h"
#include "Engine/Systems/Renderer/OpenGL/OpenGLRenderer.h"
#include "Engine/Components/Material.h"
#include "Engine/Components/Transform.h"
#include "Engine/Components/Internal/FrustumCullCache.h"

namespace Engine
{

#ifdef _DEBUG
	constexpr static bool handleDebugDraw = true;
#else
	constexpr static bool handleDebugDraw = false;
#endif

	entt::entity Scene::CreateEntity()
	{
		auto entity = registry.create();
		return entity;
	}

	void Scene::DestroyEntity(entt::entity entity)
	{
		registry.destroy(entity);
	}

	void Scene::DestroyAllEntities()
	{
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

		if constexpr (handleDebugDraw)
		{
			sceneBVH->DebugRender();
		}
	}

	void Scene::InternalSceneUpdate(double dt)
	{
		// Was doing bvh update here but its more performant to do it in the fixed update.

		if constexpr (handleDebugDraw)
		{
			// control toggle with input G key
			if (GetInputManager()->IsKeyTriggered('G'))
			{
				sceneDebugDraw->SetEnabled(!sceneDebugDraw->IsEnabled());
				std::string abled = sceneDebugDraw->IsEnabled() ? "Enabled" : "Disabled";
				std::cout << "Debug wireframe draw " << abled << "\n";
			}

			// We do clear the previous frames debug draw data though. This opens up an opportunity for caching commonly drawn wireframes.
			sceneDebugDraw->Clear();
		}
	}

	void Scene::InternalFixedPostUpdate(unsigned int tickThisSecond)
	{
		// nothing to do here
	}

}
