#include "Engine/Systems/Camera/FlyCameraController.h"

#include "Engine/Input/InputSystem.h"
#include "Engine/Systems/Camera/CameraSystem.h"

#include <algorithm>
#include <cmath>

namespace Engine
{
	FlyCameraController::FlyCameraController(Scene* scene, entt::entity owner) : FlyCameraController(scene, owner, Settings{})
	{
	}

	FlyCameraController::FlyCameraController(Scene* scene, entt::entity owner, Settings value) : Behavior(scene, owner), settings(value)
	{
	}

	int FlyCameraController::Init()
	{
		return 0;
	}

	void FlyCameraController::Apply(Camera& camera, Settings& settings, const FrameInput& input, float dt)
	{
		if (input.Look)
		{
			const float yaw = camera.GetYaw() - input.MouseDeltaX * settings.MouseSensitivity;
			const float pitch = camera.GetPitch() - input.MouseDeltaY * settings.MouseSensitivity;
			camera.SetYawPitch(yaw, pitch);
			if (input.Wheel != 0.0f)
			{
				settings.MoveSpeed = std::clamp(settings.MoveSpeed * std::pow(1.2f, input.Wheel), settings.MinSpeed, settings.MaxSpeed);
			}
		}

		const glm::vec3 forward = camera.GetForward();
		const glm::vec3 right = camera.GetRight();
		const glm::vec3 up(0.0f, 1.0f, 0.0f);
		glm::vec3 movement(0.0f);
		movement += input.Forward ? forward : glm::vec3(0.0f);
		movement -= input.Back ? forward : glm::vec3(0.0f);
		movement += input.Right ? right : glm::vec3(0.0f);
		movement -= input.Left ? right : glm::vec3(0.0f);
		movement += input.Up ? up : glm::vec3(0.0f);
		movement -= input.Down ? up : glm::vec3(0.0f);
		if (glm::dot(movement, movement) > 0.0f)
		{
			movement = glm::normalize(movement);
			const float speed = settings.MoveSpeed * (input.Boost ? settings.BoostMultiplier : 1.0f);
			camera.SetPosition(camera.GetPosition() + movement * speed * dt);
		}
	}

	void FlyCameraController::Update(double dt)
	{
		if (!input || !cameraSystem)
		{
			return;
		}
		using Swim::Platform::KeyCode;
		using Swim::Platform::MouseButton;

		const bool gated = inputGate && inputGate();
		// A look drag that started outside the UI keeps going when the pointer crosses it.
		const bool rmb = input->IsMouseButtonDown(MouseButton::Right);
		if (!rmb)
		{
			looking = false;
		}
		else if (!looking && !gated && input->IsMouseButtonTriggered(MouseButton::Right))
		{
			looking = true;
		}
		if (gated && !looking)
		{
			return;
		}

		FrameInput frame;
		frame.Look = looking;
		const auto delta = input->GetMousePositionDelta();
		frame.MouseDeltaX = delta.X;
		frame.MouseDeltaY = delta.Y;
		frame.Wheel = input->GetMouseScrollDelta();
		frame.Forward = input->IsKeyDown(KeyCode::W);
		frame.Back = input->IsKeyDown(KeyCode::S);
		frame.Left = input->IsKeyDown(KeyCode::A);
		frame.Right = input->IsKeyDown(KeyCode::D);
		frame.Up = input->IsKeyDown(KeyCode::Space);
		frame.Down = input->IsShiftDown();
		frame.Boost = input->IsControlDown();
		Apply(cameraSystem->GetCamera(), settings, frame, static_cast<float>(dt));
	}
} // namespace Engine
