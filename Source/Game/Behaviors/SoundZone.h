#pragma once

#include "Engine/Components/Tags.h"
#include "Engine/Systems/Audio/AudioSystem.h"
#include "Engine/Systems/Entity/Behavior.h"

#include <string>
#include <vector>

namespace Game
{

	// Published on the scene's event bus whenever a SoundZone's occupancy changes.
	struct SoundZoneChanged
	{
		entt::entity Zone = entt::null;
		std::uint32_t Occupants = 0;
	};

	// A trigger volume that plays a looping sound event while entities with a tag are
	// inside: it fades in on the first entry and, two seconds after the last one leaves
	// (a coroutine), fades out.
	class SoundZone : public Engine::Behavior
	{

	  public:

		SoundZone(Engine::Scene* scene, entt::entity owner, std::string eventName, Engine::TagId filter);

		int Awake() override;

		int Exit() override;

		void OnTriggerEnter(const Engine::BehaviorTrigger& trigger) override;

		void OnTriggerExit(const Engine::BehaviorTrigger& trigger) override;

		std::uint32_t GetOccupants() const { return static_cast<std::uint32_t>(inside.size()); }

	  private:


		void Publish();

		Engine::SceneTask FadeOutWhenEmpty();

		std::string eventName;
		Engine::TagId filter;
		std::vector<entt::entity> inside; // Exit can arrive after the entity is destroyed: no tag lookup then.
		Engine::VoiceHandle voice;
		Engine::TimerHandle fadeOut;

	};

} // namespace Game
