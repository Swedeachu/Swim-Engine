#pragma once

#include "Engine/Systems/Entity/Behavior.h"

#include <functional>

namespace Engine
{
	// The fly camera as a runtime behaviour (formerly the editor camera). Attach it to
	// any entity; it drives the CameraSystem's main camera:
	//
	//   hold right mouse + move   look (yaw about world up, pitch about local X)
	//   W / A / S / D             move forward / left / back / right
	//   Space / Shift             move up / down (world up)
	//   Ctrl                      boost
	//   mouse wheel (while RMB)   scale the base speed
	//
	// It uses real time, so it keeps working while the simulation is paused, slowed or
	// stopped; give its entity Playing | Paused | Stopped to fly in every state. An
	// optional input gate (for example "the UI has the pointer or keyboard") suppresses
	// it.
	class FlyCameraController : public Behavior
	{
	  public:
		struct Settings
		{
			float MoveSpeed = 5.0f;
			float BoostMultiplier = 3.0f;
			float MouseSensitivity = 0.1f; // Degrees per pixel.
			float MinSpeed = 0.25f;
			float MaxSpeed = 200.0f;
		};

		FlyCameraController(Scene* scene, entt::entity owner);
		FlyCameraController(Scene* scene, entt::entity owner, Settings settings);

		int Awake() override { return 0; }

		int Init() override;
		void Update(double dt) override;

		void FixedUpdate(unsigned int) override {}

		int Exit() override { return 0; }

		bool UsesRealTime() const override { return true; }

		Settings& GetSettings() { return settings; }

		const Settings& GetSettings() const { return settings; }

		// Returns true while the controller must ignore input.
		void SetInputGate(std::function<bool()> gate) { inputGate = std::move(gate); }

		bool IsLooking() const { return looking; }

		// Pure step used by Update (and tests): applies a frame of input to a camera.
		struct FrameInput
		{
			bool Look = false;
			float MouseDeltaX = 0.0f;
			float MouseDeltaY = 0.0f;
			float Wheel = 0.0f;
			bool Forward = false;
			bool Back = false;
			bool Left = false;
			bool Right = false;
			bool Up = false;
			bool Down = false;
			bool Boost = false;
		};

		static void Apply(class Camera& camera, Settings& settings, const FrameInput& input, float dt);

	  private:
		Settings settings;
		std::function<bool()> inputGate;
		bool looking = false;
	};
} // namespace Engine
