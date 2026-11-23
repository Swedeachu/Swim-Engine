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

	SceneTimers::Owner Behavior::OwnedTimersToken()
	{
		if (!ownedTimersToken)
		{
			ownedTimersToken = std::make_shared<int>(0);
		}

		return ownedTimersToken;
	}

	TimerHandle Behavior::After(double seconds, std::function<void()> callback)
	{
		return scene->GetTimers().After(seconds, std::move(callback), OwnedTimersToken());
	}

	TimerHandle Behavior::Every(double interval, std::function<void()> callback)
	{
		return scene->GetTimers().Every(interval, std::move(callback), OwnedTimersToken());
	}

	TimerHandle Behavior::When(std::function<bool()> predicate, std::function<void()> callback)
	{
		return scene->GetTimers().When(std::move(predicate), std::move(callback), OwnedTimersToken());
	}

	TimerHandle Behavior::StartCoroutine(SceneTask task)
	{
		return scene->GetTimers().Start(std::move(task), OwnedTimersToken());
	}

	bool Behavior::CancelTimer(TimerHandle timer)
	{
		return scene->GetTimers().Cancel(timer);
	}

} // namespace Engine
