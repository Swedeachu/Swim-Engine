#include "Engine/Components/Transform.h"
#include "Engine/Systems/Audio/AudioSystem.h"
#include "Engine/Systems/Audio/SoundEmitter.h"
#include "Game/Behaviors/Motion.h"
#include "Game/Behaviors/SoundZone.h"
#include "Game/SandboxContent.h"
#include "Game/Scenes/Sandbox.h"

#include <algorithm>
#include <iostream>

namespace Game
{

	namespace
	{

		// Development sounds from the devaudio:/ root (DevPaths.cfg); never in the repository.
		constexpr const char* Physics = "devaudio:/hl2/sound/physics/";
		const glm::vec3 PhysicsCenter{ 0.0f, 0.0f, 10.0f }; // Sandbox.cpp's physics playground.

		void RegisterSandboxSounds(Engine::AudioSystem& audio)
		{
			Engine::SoundAttenuation nearby;
			nearby.MinDistance = 2.0f;
			nearby.MaxDistance = 40.0f;

			Engine::SoundGroupDesc hard;
			hard.Sounds = { std::string(Physics) + "rubber/rubber_tire_impact_hard*.wav" };
			hard.VolumeMin = 0.7f;
			hard.PitchMin = 0.9f;
			hard.PitchMax = 1.1f;
			hard.MaxInstances = 8;
			hard.CooldownSeconds = 0.03f;
			hard.Attenuation = nearby;
			hard.Load = Engine::SoundLoad::Decoded;
			audio.RegisterGroup("impact.hard", hard);

			auto soft = hard;
			soft.Sounds = { std::string(Physics) + "rubber/rubber_tire_impact_soft*.wav" };
			soft.VolumeMin = 0.4f;
			soft.VolumeMax = 0.7f;
			audio.RegisterGroup("impact.soft", soft);

			auto metal = hard;
			metal.Sounds = { std::string(Physics) + "metal/metal_box_impact_hard*.wav" };
			audio.RegisterGroup("impact.metal", metal);

			Engine::SoundGroupDesc click;
			click.Sounds = { "devaudio:/hl2/sound/ui/buttonclick.wav" };
			click.Bus = Engine::AudioBus::Ui;
			click.Priority = 10;
			audio.RegisterGroup("ui.click", click);

			Engine::SoundGroupDesc hover;
			hover.Sounds = { "devaudio:/hl2/sound/ui/buttonrollover.wav" };
			hover.Bus = Engine::AudioBus::Ui;
			hover.VolumeMin = hover.VolumeMax = 0.5f;
			hover.CooldownSeconds = 0.05f;
			audio.RegisterGroup("ui.hover", hover);

			Engine::SoundGroupDesc machine;
			machine.Sounds = { "devaudio:/hl2/sound/ambient/machines/diesel_engine_idle1.wav" };
			machine.Loop = true;
			machine.Priority = 20;
			machine.Attenuation.MinDistance = 1.5f;
			machine.Attenuation.MaxDistance = 30.0f;
			machine.Attenuation.Doppler = 1.0f;
			audio.RegisterGroup("emitter.machine", machine);

			Engine::SoundGroupDesc ambience;
			ambience.Sounds = { "devaudio:/hl2/sound/ambient/machines/city_ventpump_loop1.wav" };
			ambience.Loop = true;
			ambience.Bus = Engine::AudioBus::Ambient;
			ambience.Priority = 30;
			ambience.Load = Engine::SoundLoad::Stream;
			ambience.Attenuation.MinDistance = 6.0f;
			ambience.Attenuation.MaxDistance = 60.0f;
			audio.RegisterGroup("ambient.zone", ambience);

			audio.MapEvent("physics.impact.hard", "impact.hard");
			audio.MapEvent("physics.impact.soft", "impact.soft");
			audio.MapEvent("ui.activate", "ui.click");
		}

	} // namespace

	void Sandbox::BuildTriggerZone()
	{
		// A trigger volume over the ball drop area: balls inside start a sound loop
		// (SoundZone: trigger callbacks, a coroutine fade-out and an event-bus count).
		const auto zone = SpawnMesh(*this,
			{ "Sound zone", palette.Mesh(Engine::BuiltinMesh::Cube),
				Mat("Sound zone", glm::vec3(0.3f, 0.8f, 1.0f), 0.0f, 0.1f, glm::vec3(0.0f), true, 0.12f),
				PhysicsCenter + glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(7.0f, 3.0f, 7.0f), glm::quat(1, 0, 0, 0),
				Swim::Render::RenderObjectFlags::Default, { GameTags::PhysicsToy } });
		Engine::Rigidbody body;
		body.type = Engine::RigidbodyType::Static;
		body.isTrigger = true;
		body.useGravity = false;
		body.collider.type = Engine::ColliderType::Box;
		body.collider.box.halfExtents = glm::vec3(0.5f);
		AddComponent<Engine::Rigidbody>(zone, body);
		AddTag(zone, Engine::Tags::Physics);
		EmplaceBehavior<SoundZone>(zone, "ambient.zone", Engine::Tags::Projectile);
		zoneOccupants = 0;
		zoneSubscription = GetEvents().Subscribe<SoundZoneChanged>(
			[this](const SoundZoneChanged& changed)
			{
				zoneOccupants = changed.Occupants;
			});
	}

	void Sandbox::BuildAudioPlayground()
	{
		BuildTriggerZone();
		auto* audio = GetAudio();

		if (!audio)
		{
			return;
		}

		if (!audio->IsRootMounted("devaudio"))
		{
			if (builds == 1)
			{
				std::cout << "[Sandbox] DevAudioRoot is not set (configure with -DSWIM_DEV_AUDIO_ROOT=<folder>); the audio playground is "
							 "silent.\n";
			}

			return;
		}

		RegisterSandboxSounds(*audio);

		// A sound that circles the physics playground: listen to it pan and pass by.
		const auto emitter = SpawnMesh(*this,
			{ "Sound emitter", palette.Mesh(Engine::BuiltinMesh::Sphere),
				Mat("Sound emitter", glm::vec3(0.2f, 0.6f, 1.0f), 0.0f, 0.3f, glm::vec3(0.4f, 1.2f, 3.0f)), PhysicsCenter, glm::vec3(0.35f) });
		EmplaceBehavior<Orbit>(emitter, PhysicsCenter, 7.0f, 2.0f, 0.6f, 0.0f);
		EmplaceBehavior<Engine::SoundEmitter>(emitter, "emitter.machine", true, 0.8f);
	}

	void Sandbox::PlayImpactSound(float impulse, const glm::vec3& position)
	{
		auto* audio = GetAudio();

		if (!audio || impulse < 0.5f)
		{
			return;
		}

		Engine::SoundEventParams params;
		params.Position = position;
		params.Volume = std::clamp(impulse / 12.0f, 0.15f, 1.0f);
		audio->PlayEvent(impulse > 6.0f ? "physics.impact.hard" : "physics.impact.soft", params);
	}

} // namespace Game
