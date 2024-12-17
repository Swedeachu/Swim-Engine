#include "PCH.h"
#include "Scene.h"
#include "Engine/Systems/Renderer/VulkanRenderer.h"

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

}
