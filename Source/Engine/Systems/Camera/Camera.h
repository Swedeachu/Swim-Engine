#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Engine/Math/Ray.h"

#include <array>
#include <optional>
#include <cstdint>

namespace Engine
{

	// A world ray (normalized direction) through a screen point: Engine::Ray.
	using CameraRay = Ray;

	// A perspective camera in the engine's conventions: right-handed world space with
	// +Y up, looking down its local -Z; the projection is the renderer's canonical
	// infinite-far reverse-Z (near plane at depth 1). FarPlane only bounds effects that
	// need a finite range (light clusters, shadows, fog).
	class Camera
	{

	  public:

		void SetPosition(const glm::vec3& value) { position = value; }

		const glm::vec3& GetPosition() const { return position; }

		void SetRotation(const glm::quat& value);

		const glm::quat& GetRotation() const { return rotation; }

		// Yaw about world +Y, then pitch about the local X axis (degrees; pitch clamped to +-89.9).
		void SetYawPitch(float yawDegrees, float pitchDegrees);

		float GetYaw() const { return yaw; }

		float GetPitch() const { return pitch; }

		void LookAt(const glm::vec3& eye, const glm::vec3& target);

		// Vertical field of view in degrees (1 .. 170).
		void SetFieldOfView(float degrees);

		float GetFieldOfView() const { return fieldOfView; }

		void SetClipPlanes(float nearPlane, float farPlane);

		float GetNearPlane() const { return nearPlane; }

		float GetFarPlane() const { return farPlane; }

		void SetAspect(float value);

		float GetAspect() const { return aspect; }

		glm::vec3 GetForward() const;

		glm::vec3 GetRight() const;

		glm::vec3 GetUp() const;

		// World -> view (rigid) and view -> clip (reverse-Z, infinite far), column-major glm.
		glm::mat4 GetViewMatrix() const;

		glm::mat4 GetProjectionMatrix() const;

		// The same as row-major float[16] (clip = dot(row, float4(p, 1))), the renderer's layout.
		std::array<float, 16> GetViewRowMajor() const;

		std::array<float, 16> GetProjectionRowMajor() const;

		std::array<float, 16> GetViewProjectionRowMajor() const;

		// A ray through a pixel (top-left origin) of a width x height viewport.
		CameraRay ScreenPointToRay(float x, float y, float width, float height) const;

		// Screen <-> world (pixels, top-left origin, of a width x height viewport):
		//  - WorldToScreen: the pixel a world point projects to, with its view depth (distance
		//    along the camera's forward axis) in z; nothing for points at or behind the camera
		//    plane. Points off screen still project (x or y outside the viewport).
		//  - ScreenToWorld: the world point `distance` along the pixel's ray.
		//  - ScreenToWorldAtDepth: the world point at view depth `depth` under the pixel (the
		//    inverse of WorldToScreen).
		std::optional<glm::vec3> WorldToScreen(const glm::vec3& world, float width, float height) const;

		glm::vec3 ScreenToWorld(float x, float y, float distance, float width, float height) const;

		glm::vec3 ScreenToWorldAtDepth(float x, float y, float depth, float width, float height) const;

		// Pixel <-> normalized device coordinates (x right, y up, [-1, 1] across the viewport).
		static glm::vec2 ScreenToNdc(float x, float y, float width, float height);

		static glm::vec2 NdcToScreen(const glm::vec2& ndc, float width, float height);

	  private:

		glm::vec3 position{ 0.0f, 2.0f, 8.0f };
		glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		float yaw = 0.0f;
		float pitch = 0.0f;
		float fieldOfView = 60.0f;
		float nearPlane = 0.1f;
		float farPlane = 500.0f;
		float aspect = 16.0f / 9.0f;

	};

	// Row-major float[16] of a column-major glm matrix.
	std::array<float, 16> ToRowMajor(const glm::mat4& matrix);

} // namespace Engine
