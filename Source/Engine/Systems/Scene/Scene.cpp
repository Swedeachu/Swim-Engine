#include "PCH.h"
#include "Scene.h"
#include "Engine/Systems/Renderer/Vulkan/VulkanRenderer.h"
#include "Engine/Components/Material.h"
#include "Engine/Components/Transform.h"
#include "Engine/Components/Internal/FrustumCullCache.h"

namespace Engine
{

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
			Transform::ClearGlobalDirtyFlag(); // right now doing this here in internal update before Scene::Update prevents gameplay code from leveraging this flag.
		}

		frustumCacheObserver.clear();
	}

	void Scene::InternalSceneUpdate(double dt)
	{

	}

}
