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

  void Scene::SubmitMeshesToRenderer()
  {
    auto renderer = GetRenderer();
    if (!renderer)
    {
      throw std::runtime_error("Renderer not available");
    }

    auto view = registry.view<Transform, Mesh>();
    for (auto entity : view)
    {
      const auto& transform = view.get<Transform>(entity);
      const auto& mesh = view.get<Mesh>(entity);
      renderer->SubmitMesh(transform, mesh);
    }

    // Record command buffers after submission
    renderer->RecordCommandBuffers();
  }

}
