#pragma once
#include "Library/glm/glm.hpp"
#include "Library/glm/gtc/quaternion.hpp"
#include "Library/glm/gtc/matrix_transform.hpp"
#include "Engine/SwimEngine.h"

namespace Engine
{

	struct CameraUBO
	{
		glm::mat4 view;   // 64 bytes
		glm::mat4 proj;   // 64 bytes
		glm::vec4 camParams; // 16 bytes: (fovX, fovY, zNear, zFar)
	};

	// this code was thrown together very quickly, but honestly a camera should just be using the premade transform component 
	// maybe camera deserves its own file
	// later on it should be a component which can be used for making different render targets/layers to send draw calls to with its own specific world spaces
	// that could do some really cool stuff like portals, mirrors, etc (shader would probably be best for most that stuff though)
	struct Camera
	{

	private:

		glm::vec3 position{ 0.0f };
		glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f }; // Identity quaternion

		float fov = 45.0f;
		float aspect = 1.0f;
		float nearClip = 0.1f;
		float farClip = 100.0f;

		mutable bool viewDirty = true;
		mutable bool projDirty = true;
		mutable glm::mat4 viewMatrix{ 1.0f };
		mutable glm::mat4 projMatrix{ 1.0f };

		void MarkViewDirty() { viewDirty = true; }
		void MarkProjDirty() { projDirty = true; }

	public:

		void SetPosition(const glm::vec3& pos) { position = pos; MarkViewDirty(); }
		void SetRotation(const glm::quat& rot) { rotation = rot; MarkViewDirty(); }

		void SetRotationEuler(float pitch, float yaw, float roll)
		{
			rotation = glm::quat(glm::vec3(glm::radians(pitch), glm::radians(yaw), glm::radians(roll)));
			MarkViewDirty();
		}

		// Returns pitch, yaw, roll in degrees
		glm::vec3 GetRotationEuler() const
		{
			glm::vec3 euler = glm::degrees(glm::eulerAngles(rotation));
			return euler;
		}

		void SetFOV(float f) { fov = f; MarkProjDirty(); }
		void SetAspect(float a) { aspect = a; MarkProjDirty(); }
		void SetClipPlanes(float nearC, float farC) { nearClip = nearC; farClip = farC; MarkProjDirty(); }

		const glm::vec3& GetPosition() const { return position; }
		const glm::quat& GetRotation() const { return rotation; }
		float GetFOV() const { return fov; }
		float GetAspect() const { return aspect; }
		float GetNearClip() const { return nearClip; }
		float GetFarClip() const { return farClip; }

		// View matrix recalculated lazily
		const glm::mat4& GetViewMatrix() const
		{
			if (viewDirty)
			{
				glm::mat4 rotationMatrix = glm::mat4_cast(rotation);
				glm::vec3 forward = rotationMatrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
				viewMatrix = glm::lookAt(position, position + forward, glm::vec3(0.0f, 1.0f, 0.0f));
				viewDirty = false;
			}
			return viewMatrix;
		}

		// Projection matrix recalculated lazily
		const glm::mat4& GetProjectionMatrix() const
		{
			if (projDirty)
			{
				projMatrix = glm::perspective(glm::radians(fov), aspect, nearClip, farClip);
				if constexpr (SwimEngine::CONTEXT == SwimEngine::RenderContext::Vulkan) projMatrix[1][1] *= -1; // Vulkan clip space correction
				projDirty = false;
			}
			return projMatrix;
		}
	};

	class CameraSystem : public Machine
	{

	public:

		CameraSystem();

		int Init() override;
		void Update(double dt) override;
		void RefreshAspect();

		const glm::mat4& GetViewMatrix() const { return camera.GetViewMatrix(); }

		const glm::mat4& GetProjectionMatrix() const { return camera.GetProjectionMatrix(); }

		// By reference so you can't set the camera to null or any craziness
		Camera& GetCamera() { return camera; }

	private:

		Camera camera;

	};

}
