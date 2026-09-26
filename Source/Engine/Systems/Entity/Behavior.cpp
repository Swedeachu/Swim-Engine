#include "Engine/Systems/Entity/Behavior.h"

#include "Engine/Components/Transform.h"
#include "Engine/Systems/Scene/Scene.h"

#include <stdexcept>

namespace Engine
{
	Behavior::Behavior(Scene* sceneValue, entt::entity owner) : scene(sceneValue), entity(owner)
	{
		if (scene == nullptr || entity == entt::null)
		{
			throw std::runtime_error("Behavior requires a valid Scene and entt::entity.");
		}
		RefreshFieldCache();
	}

	void Behavior::RefreshFieldCache()
	{
		input = scene->GetInputSystem();
		cameraSystem = scene->GetCameraSystem();
	}

	Transform* Behavior::GetTransform() const
	{
		auto& registry = scene->GetRegistry();
		return registry.valid(entity) ? registry.try_get<Transform>(entity) : nullptr;
	}

	const SimulationFrame& Behavior::GetTime() const
	{
		return scene->GetTime();
	}
} // namespace Engine
