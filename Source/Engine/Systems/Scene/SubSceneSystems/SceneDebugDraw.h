#pragma once

#include "Library/EnTT/entt.hpp"
#include "Library/glm/glm.hpp"

namespace Engine
{

	class Mesh;

	// ========================================================================================
	// DebugWireBoxData component
	// Represents any extra data for a wireframe box beyond just the transform, such as color.
	// Currently color is the only thing we have, transform component does most the work.
	// ========================================================================================
	struct DebugWireBoxData
	{
		glm::vec3 color; // RGB color (0..1 range)
	};

	class SceneDebugDraw
	{

	public:

		SceneDebugDraw() = default;

		// Initializes the debug draw system and registers the wireframe cube mesh.
		void Init();

		// Clears all wireframe draw requests. Should be called once per frame.
		void Clear();

		void SetEnabled(bool value) { enabled = value; }
		const bool IsEnabled() const { return enabled; }

		// Submits a wireframe box using min/max AABB corners.
		// color defaults to red if not supplied.
		void SubmitWireframeBoxAABB
		(
			const glm::vec3& min,
			const glm::vec3& max,
			const glm::vec3& color = glm::vec3(1.0f, 0.0f, 0.0f)
		);

		// Submits a wireframe box using a position, scale, and optional Euler rotation (pitch, yaw, roll in degrees).
		// color defaults to red if not supplied.
		void SubmitWireframeBox
		(
			const glm::vec3& position,
			const glm::vec3& scale,
			float pitchDegrees = 0.0f,
			float yawDegrees = 0.0f,
			float rollDegrees = 0.0f,
			const glm::vec3& color = glm::vec3(1.0f, 0.0f, 0.0f)
		);

		// Accessor to the internal debug registry.
		entt::registry& GetRegistry();

		// Accessor to the shared wireframe cube mesh.
		const std::shared_ptr<Mesh>& GetWireframeCubeMesh() const;

	private:

		bool enabled{ false };
		entt::registry debugRegistry;                      // Temporary draw call storage
		std::shared_ptr<Mesh> wireframeCubeMesh = nullptr; // Cached wireframe cube mesh

	};

}
