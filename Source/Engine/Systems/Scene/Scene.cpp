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

    // Reset for the frame on a clean state
    renderer->ClearFrameRenderables();

    auto view = registry.view<Transform, Mesh>();
    for (auto entity : view)
    {
      const auto& transform = view.get<Transform>(entity);
      const auto& mesh = view.get<Mesh>(entity);

      // Add each mesh + transform to the renderer for this frame
      renderer->AddRenderable(transform, mesh);
    }
  }

}
