#pragma once

#include "Engine/Systems/Entity/Behavior.h"

#include <cstdint>
#include <functional>

namespace Game
{
	// Fires physics balls from the camera along its view direction while F is held (when the
	// UI does not own the keyboard), or through sandbox.fire. Balls live 12 simulated seconds.
	class BallShooter : public Engine::Behavior
	{
	  public:
		BallShooter(Engine::Scene* scene, entt::entity owner);
		void Update(double dt) override;

		void SetInputGate(std::function<bool()> gate) { inputGate = std::move(gate); }

		// Queues one ball from the camera now (the panel's buttons use it too).
		void Fire(float speed = 18.0f);

		std::uint64_t GetFired() const { return fired; }

	  private:
		std::function<bool()> inputGate;
		std::uint64_t fired = 0;
		float cooldown = 0.0f;
	};

	// A fired ball: reports its impacts to the sandbox (collision callbacks).
	class Projectile : public Engine::Behavior
	{
	  public:
		using Engine::Behavior::Behavior;
		int Awake() override;
		void OnCollisionEnter(const Engine::BehaviorCollision& collision) override;
	};
} // namespace Game
