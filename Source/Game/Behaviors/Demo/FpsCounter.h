#pragma once

#include "Engine/Systems/Entity/Behavior.h"
#include "Engine/Components/TextComponent.h"
#include "Library/glm/vec3.hpp"
#include <memory>

namespace Game
{

	class FpsCounter : public Engine::Behavior
	{

	public:

		using Engine::Behavior::Behavior;

		explicit FpsCounter(Engine::Scene* scene, entt::entity owner, bool chroma)
			: Engine::Behavior(scene, owner), chroma(chroma)
		{}

		int Awake() override { return 0; }
		int Init() override;
		void Update(double dt) override;
		void FixedUpdate(unsigned int) override {}
		int Exit() override { return 0; }

	private:

		bool chroma = false;

		std::shared_ptr<Engine::SwimEngine> engine;
		Engine::TextComponent* tc = nullptr;

		// Accumulates elapsed time while alive; drives hue rotation.
		double chromaTime = 0.0;

		// Stable per-entity hue offset so multiple counters don't match phases.
		float chromaStartHue = 0.0f;

		// Convert HSV to RGB (h in [0,1), s,v in [0,1]). Returns linear RGB.
		static glm::vec3 HSVtoRGB(float h, float s, float v);

	};

}
