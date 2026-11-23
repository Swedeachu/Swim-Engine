#pragma once

#include "Engine/Systems/Audio/AudioSystem.h"
#include "Engine/Systems/Entity/Behavior.h"

#include <string>

namespace Engine
{

	// Plays a sound group (or event) at its entity and keeps the voice on the entity's
	// position; the voice stops (with a short fade) when the behaviour exits. A looping
	// group plays until then; Play/Stop restart or end it from gameplay.
	class SoundEmitter : public Behavior
	{

	  public:

		SoundEmitter(Scene* scene, entt::entity owner, std::string eventName, bool playOnInit = true, float volume = 1.0f);

		int Init() override;

		void Update(double dt) override;

		int Exit() override;

		void Play();

		void Stop(float fadeSeconds = 0.1f);

		bool IsPlaying() const;

		VoiceHandle GetVoice() const { return voice; }

	  private:

		glm::vec3 Position() const;

		std::string eventName;
		bool playOnInit;
		float volume;
		VoiceHandle voice;
		glm::vec3 lastPosition{ 0.0f };

	};

} // namespace Engine
