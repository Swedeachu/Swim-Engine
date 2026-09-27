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

	void FlyCameraController::ApplyZoom(Camera& camera, ZoomState& state, const Settings& settings, float wheel, bool reset, float dt)
	{
		const float current = camera.GetFieldOfView();
		if (state.BaseFieldOfView <= 0.0f || std::abs(current - state.AppliedFieldOfView) > 1.0e-3f)
		{
			// First use, or the field of view was set elsewhere (a bookmark, a script): that
			// becomes the view at the current zoom.
			state.BaseFieldOfView = current * state.Zoom;
		}
		const float minZoom = std::max(settings.MinZoom, 1.0e-3f);
		const float maxZoom = std::max(settings.MaxZoom, minZoom);
		if (reset)
		{
			state.Target = 1.0f;
		}
		if (std::isfinite(wheel) && wheel != 0.0f)
		{
			state.Target = std::clamp(state.Target * std::pow(settings.ZoomStep, wheel), minZoom, maxZoom);
		}
		const float blend = settings.ZoomSmoothing > 0.0f && dt > 0.0f ? 1.0f - std::exp(-dt / settings.ZoomSmoothing) : 1.0f;
		const float logZoom = std::log(state.Zoom) + (std::log(state.Target) - std::log(state.Zoom)) * std::clamp(blend, 0.0f, 1.0f);
		state.Zoom = std::abs(logZoom - std::log(state.Target)) < 1.0e-4f ? state.Target : std::exp(logZoom);
		const float fieldOfView = std::clamp(state.BaseFieldOfView / state.Zoom, 1.0f, std::min(settings.MaxFieldOfView, 170.0f));
		camera.SetFieldOfView(fieldOfView);
		state.AppliedFieldOfView = camera.GetFieldOfView();
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
		// Wheel zoom and middle-click reset (not while looking: then the wheel sets the speed,
		// and not while the UI has the pointer, which scrolls its panels). The easing runs
		// every frame so a zoom finishes even after the pointer moves onto the UI.
		const float wheel = !looking && !gated ? input->GetMouseScrollDelta() : 0.0f;
		const bool resetZoom = !gated && input->IsMouseButtonTriggered(MouseButton::Middle);
		ApplyZoom(cameraSystem->GetCamera(), zoom, settings, wheel, resetZoom, static_cast<float>(dt));
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
