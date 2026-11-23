#include "Game/Behaviors/SoundZone.h"

#include "Engine/Components/Transform.h"
#include "Engine/Systems/Scene/Scene.h"

#include <algorithm>

namespace Game
{

	SoundZone::SoundZone(Engine::Scene* sceneValue, entt::entity owner, std::string eventNameValue, Engine::TagId filterValue)
		: Behavior(sceneValue, owner), eventName(std::move(eventNameValue)), filter(filterValue)
	{
	}

	int SoundZone::Awake()
	{
		EnableCollisionCallBacks(true);
		return 0;
	}

	int SoundZone::Exit()
	{
		if (auto* audio = GetScene().GetAudio())
		{
			audio->Stop(voice, 0.2f);
		}

		return 0;
	}

	void SoundZone::Publish()
	{
		GetScene().GetEvents().Publish(SoundZoneChanged{ GetEntity(), GetOccupants() });
	}

	void SoundZone::OnTriggerEnter(const Engine::BehaviorTrigger& trigger)
	{
		if (trigger.Trigger != GetEntity() || !GetScene().HasTag(trigger.Other, filter))
		{
			return;
		}

		inside.push_back(trigger.Other);
		Publish();
		CancelTimer(fadeOut);
		fadeOut = {};
		auto* audio = GetScene().GetAudio();

		if (inside.size() == 1 && audio && !audio->IsPlaying(voice))
		{
			Engine::SoundEventParams params;
			params.FadeInSeconds = 1.0f;

			if (const auto* transform = GetTransform())
			{
				params.Position = transform->GetPosition();
			}

			voice = audio->PlayEvent(eventName, params);
		}
	}

	void SoundZone::OnTriggerExit(const Engine::BehaviorTrigger& trigger)
	{
		const auto found = std::find(inside.begin(), inside.end(), trigger.Other);

		if (trigger.Trigger != GetEntity() || found == inside.end())
		{
			return;
		}

		inside.erase(found);
		Publish();

		if (inside.empty())
		{
			fadeOut = StartCoroutine(FadeOutWhenEmpty());
		}
	}

	Engine::SceneTask SoundZone::FadeOutWhenEmpty()
	{
		co_await Engine::Wait(2.0);

		if (inside.empty())
		{
			if (auto* audio = GetScene().GetAudio())
			{
				audio->Stop(voice, 1.5f);
			}

			voice = {};
		}
	}

} // namespace Game
