#pragma once

#include "Engine/Systems/Entity/Behavior.h"
#include "Engine/Systems/Renderer/Features/GravitationalLensing.h"

#include <functional>
#include <memory>
#include <optional>

namespace Game
{

	// A black hole: every frame its entity's world position and orientation (local +Y is the
	// gas disk's axis) become a lens of the GravitationalLensing render feature, which traces
	// its shadow, the bent light around it and its volumetric gas. Lens settings other than
	// the position, radius and axis (gas density, speed, brightness, rings) stay editable
	// on the feature.
	class BlackHole : public Engine::Behavior
	{

	  public:

		BlackHole(Engine::Scene* scene, entt::entity owner, std::shared_ptr<Engine::GravitationalLensing> lensing,
			float schwarzschildRadius);

		void Update(double dt) override;

		int Exit() override;

		bool UsesRealTime() const override { return true; }

		float GetSchwarzschildRadius() const { return schwarzschildRadius; }

		// Sets the lens's gas density on the next update (the lens exists from then on).
		void SetGasDensity(float density) { pendingGasDensity = density; }

		// The shadow's radius (photon capture): what picking and dragging should grab.
		float GetShadowRadius() const { return Engine::GravitationalLensing::ShadowRadius(schwarzschildRadius); }

	  private:

		std::uint64_t Key() const;

		std::weak_ptr<Engine::GravitationalLensing> lensing;
		float schwarzschildRadius;
		std::optional<float> pendingGasDensity;

	};

	// Drags its entity with the left mouse button: a press whose closest pick
	// (Engine::ScenePicking) is this entity or one of its children grabs it; while held,
	// the entity follows the mouse ray on the camera-facing plane through its centre.
	// The input gate (the UI owning the pointer) blocks new grabs; the right button still
	// flies the camera and F still fires the ball shooter.
	class MouseDrag : public Engine::Behavior
	{

	  public:

		MouseDrag(Engine::Scene* scene, entt::entity owner);

		void Update(double dt) override;

		bool UsesRealTime() const override { return true; }

		void SetInputGate(std::function<bool()> gate) { inputGate = std::move(gate); }

		bool IsDragging() const { return dragging; }

		// Mouse input as the behaviour sees it (tests drive it without a window).
		void Press(float x, float y);

		void Move(float x, float y);

		void Release();

	  private:

		bool Owns(entt::entity hit) const;

		std::function<bool()> inputGate;
		bool dragging = false;
		glm::vec3 planePoint{ 0.0f };
		glm::vec3 planeNormal{ 0.0f, 0.0f, 1.0f };
		glm::vec3 grabOffset{ 0.0f };

	};

} // namespace Game
