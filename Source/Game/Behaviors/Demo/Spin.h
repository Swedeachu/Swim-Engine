#pragma once

#include "Engine/Systems/Entity/Behavior.h"

namespace Game
{

	class Spin : public Engine::Behavior
	{

	public:

		using Engine::Behavior::Behavior;

		explicit Spin(Engine::Scene* scene, entt::entity owner, float speed = 90.0f)
			: Engine::Behavior(scene, owner), spinSpeed(speed)
		{}

		int Awake() override { return 0; }

		int Init() override { return 0; }

		void Update(double dt) override;

		void FixedUpdate(unsigned int) override {}

		int Exit() override { return 0; }

	private:

		float spinSpeed = 90.0f; // degrees per second
		float accumulatedAngle = 0.0f;

	};

}
