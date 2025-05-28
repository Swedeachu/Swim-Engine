#include "PCH.h"
#include "Behavior.h"

#include "Engine/Systems/Scene/Scene.h"
#include "Engine/Systems/Scene/SceneSystem.h"
#include "Engine/Systems/IO/InputManager.h"
#include "Engine/Systems/Renderer/Core/Camera/CameraSystem.h"
#include "Engine/Systems/Renderer/Renderer.h"
#include "Engine/Components/Material.h"
#include "Engine/Components/Transform.h"

namespace Engine
{

	// We might want to defer behavior creation like this to a method, for example if we have factory archetypes that won't belong to a scene.
	Behavior::Behavior(Scene* scene, entt::entity owner) : scene(scene), entity(owner)
	{
		if (scene == nullptr || entity == entt::null)
		{
			throw std::runtime_error("Behavior requires a valid Scene and entt::entity.");
		}

		// Cache commonly used systems
		input = scene->GetInputManager();
		sceneSystem = scene->GetSceneSystem();
		cameraSystem = scene->GetCameraSystem();
		renderer = scene->GetRenderer();

		// Cache common crucial components if they exist
		auto& registry = scene->GetRegistry();

		if (registry.any_of<Transform>(entity))
		{
			transform = &registry.get<Transform>(entity);
		}
		else
		{
			transform = nullptr;
		}

		if (registry.any_of<Material>(entity))
		{
			material = &registry.get<Material>(entity);
		}
		else
		{
			material = nullptr;
		}
	}

}