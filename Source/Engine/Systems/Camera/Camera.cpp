#include "Engine/Systems/Camera/Camera.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Engine
{
	std::array<float, 16> ToRowMajor(const glm::mat4& matrix)
	{
		std::array<float, 16> rows{};
		for (int row = 0; row < 4; ++row)
		{
			for (int column = 0; column < 4; ++column)
			{
				rows[static_cast<std::size_t>(row * 4 + column)] = matrix[column][row];
			}
		}
		return rows;
	}

	void Camera::SetRotation(const glm::quat& value)
	{
		rotation = glm::normalize(value);
		// Recover yaw/pitch so later relative mouse look continues from here.
		const glm::vec3 forward = GetForward();
		pitch = glm::degrees(std::asin(std::clamp(forward.y, -1.0f, 1.0f)));
		yaw = glm::degrees(std::atan2(-forward.x, -forward.z));
	}

	void Camera::SetYawPitch(float yawDegrees, float pitchDegrees)
	{
		yaw = yawDegrees;
		pitch = std::clamp(pitchDegrees, -89.9f, 89.9f);
		const glm::quat yawRotation = glm::angleAxis(glm::radians(yaw), glm::vec3(0.0f, 1.0f, 0.0f));
		const glm::quat pitchRotation = glm::angleAxis(glm::radians(pitch), glm::vec3(1.0f, 0.0f, 0.0f));
		rotation = glm::normalize(yawRotation * pitchRotation);
	}

	void Camera::LookAt(const glm::vec3& eye, const glm::vec3& target)
	{
		position = eye;
		const glm::vec3 direction = target - eye;
		const float length = glm::length(direction);
		if (length <= 1e-6f)
		{
			return;
		}
		const glm::vec3 forward = direction / length;
		SetYawPitch(glm::degrees(std::atan2(-forward.x, -forward.z)), glm::degrees(std::asin(std::clamp(forward.y, -1.0f, 1.0f))));
	}

	void Camera::SetFieldOfView(float degrees)
	{
		if (!std::isfinite(degrees) || degrees < 1.0f || degrees > 170.0f)
		{
			throw std::invalid_argument("Camera field of view must be 1 .. 170 degrees");
		}
		fieldOfView = degrees;
	}

	void Camera::SetClipPlanes(float nearValue, float farValue)
	{
		if (!std::isfinite(nearValue) || !std::isfinite(farValue) || nearValue <= 0.0f || farValue <= nearValue)
		{
			throw std::invalid_argument("Camera clip planes need 0 < near < far");
		}
		nearPlane = nearValue;
		farPlane = farValue;
	}

	void Camera::SetAspect(float value)
	{
		if (std::isfinite(value) && value > 0.0f)
		{
			aspect = value;
		}
	}

	glm::vec3 Camera::GetForward() const
	{
		return glm::normalize(rotation * glm::vec3(0.0f, 0.0f, -1.0f));
	}

	glm::vec3 Camera::GetRight() const
	{
		return glm::normalize(rotation * glm::vec3(1.0f, 0.0f, 0.0f));
	}

	glm::vec3 Camera::GetUp() const
	{
		return glm::normalize(rotation * glm::vec3(0.0f, 1.0f, 0.0f));
	}

	glm::mat4 Camera::GetViewMatrix() const
	{
		// Inverse of the rigid camera transform: R^T * T(-p).
		const glm::mat4 inverseRotation = glm::mat4_cast(glm::conjugate(rotation));
		return inverseRotation * glm::translate(glm::mat4(1.0f), -position);
	}

	glm::mat4 Camera::GetProjectionMatrix() const
	{
		// Matches Swim::Render::PerspectiveReverseZRowMajor: clip z = near, clip w = -z_view.
		const float f = 1.0f / std::tan(glm::radians(fieldOfView) * 0.5f);
		glm::mat4 m(0.0f);
		m[0][0] = f / aspect;
		m[1][1] = f;
		m[3][2] = nearPlane; // Row 2, column 3.
		m[2][3] = -1.0f;	 // Row 3, column 2.
		return m;
	}

	std::array<float, 16> Camera::GetViewRowMajor() const
	{
		return ToRowMajor(GetViewMatrix());
	}

	std::array<float, 16> Camera::GetProjectionRowMajor() const
	{
		return ToRowMajor(GetProjectionMatrix());
	}

	std::array<float, 16> Camera::GetViewProjectionRowMajor() const
	{
		return ToRowMajor(GetProjectionMatrix() * GetViewMatrix());
	}

	CameraRay Camera::ScreenPointToRay(float x, float y, float width, float height) const
	{
		CameraRay ray;
		ray.Origin = position;
		if (width <= 0.0f || height <= 0.0f)
		{
			ray.Direction = GetForward();
			return ray;
		}
		const float ndcX = 2.0f * x / width - 1.0f;
		const float ndcY = 1.0f - 2.0f * y / height;
		const float tanHalf = std::tan(glm::radians(fieldOfView) * 0.5f);
		const glm::vec3 viewDirection(ndcX * tanHalf * aspect, ndcY * tanHalf, -1.0f);
		ray.Direction = glm::normalize(rotation * viewDirection);
		return ray;
	}
} // namespace Engine
