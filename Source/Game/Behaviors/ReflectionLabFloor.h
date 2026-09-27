#pragma once

#include "Engine/Systems/Entity/Behavior.h"
#include "Engine/Systems/Renderer/Runtime/MaterialLibrary.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace Game
{
	// The reflection lab's floor pad, whose look changes at run time so the chrome around it
	// proves that reflections follow the real scene (local probes re-capture it) rather than
	// a fixed environment colour:
	//
	//   Checker   black and white tiles
	//   Green     green tiles
	//   Rainbow   the tiles' tint cycles through the hues (once every RainbowPeriod seconds)
	//   Removed   the pad is hidden: the chrome shows the sandy ground under it
	//
	// Runs on real time (it animates while paused too). Set by "sandbox.labfloor <0-3>" and
	// the Rendering tab.
	class ReflectionLabFloor : public Engine::Behavior
	{
	  public:
		enum class Mode : std::uint32_t
		{
			Checker = 0,
			Green = 1,
			Rainbow = 2,
			Removed = 3,
			Count = 4,
		};

		static constexpr float RainbowPeriod = 6.0f;

		ReflectionLabFloor(Engine::Scene* scene, entt::entity owner, std::uint32_t materialSet, Engine::MaterialDesc material);

		int Init() override;
		void Update(double dt) override;

		bool UsesRealTime() const override { return true; }

		void SetMode(Mode value);

		Mode GetMode() const { return mode; }

		// The tint the tiles have now (linear; multiplies the checker texture).
		glm::vec3 GetTint() const { return tint; }

		// The tint of a mode at a time (seconds).
		static glm::vec3 TintFor(Mode mode, float time);

	  private:
		void Apply();

		std::uint32_t materialSet;
		Engine::MaterialDesc material;
		Mode mode = Mode::Checker;
		float time = 0.0f;
		glm::vec3 tint{ 1.0f };
		bool visible = true;
	};
} // namespace Game
