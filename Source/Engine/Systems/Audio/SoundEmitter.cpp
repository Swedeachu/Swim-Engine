#include "Engine/Systems/Audio/SoundEmitter.h"

#include "Engine/Components/Transform.h"
#include "Engine/Systems/Scene/Scene.h"

namespace Engine
{

	SoundEmitter::SoundEmitter(Scene* sceneValue, entt::entity owner, std::string eventNameValue, bool playOnInitValue, float volumeValue)
		: Behavior(sceneValue, owner), eventName(std::move(eventNameValue)), playOnInit(playOnInitValue), volume(volumeValue)
	{
	}

	glm::vec3 SoundEmitter::Position() const
	{
		const auto* transform = GetScene().GetRegistry().try_get<Transform>(GetEntity());
		return transform ? transform->GetPosition() : glm::vec3(0.0f);
	}

	int SoundEmitter::Init()
	{
		lastPosition = Position();

		if (playOnInit)
		{
			Play();
		}

		return 0;
	}

	void SoundEmitter::Update(double dt)
	{
		auto* audio = GetScene().GetAudio();

		if (!audio || !audio->IsPlaying(voice))
		{
			return;
		}

		const auto position = Position();
		const auto velocity = dt > 1.0e-5 ? (position - lastPosition) / static_cast<float>(dt) : glm::vec3(0.0f);
		lastPosition = position;
		audio->SetPosition(voice, position, velocity);
	}

	int SoundEmitter::Exit()
	{
		Stop();
		return 0;
	}

	void SoundEmitter::Play()
	{
		auto* audio = GetScene().GetAudio();

		if (!audio)
		{
			return;
		}

		audio->Stop(voice, 0.05f);
		SoundEventParams params;
		params.Position = Position();
		params.Volume = volume;
		voice = audio->PlayEvent(eventName, params);
	}

	void SoundEmitter::Stop(float fadeSeconds)
	{
		if (auto* audio = GetScene().GetAudio())
		{
			audio->Stop(voice, fadeSeconds);
		}

		voice = {};
	}

	bool SoundEmitter::IsPlaying() const
	{
		const auto* audio = GetScene().GetAudio();
		return audio && audio->IsPlaying(voice);
	}

} // namespace Engine
