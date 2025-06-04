#pragma once 

#include "array"
#include "memory"
#include "Engine/Systems/Renderer/Core/Textures/Texture2D.h"

namespace Engine
{

	// Cube vertices for skybox (size doesn't matter, we scale it in shader/view matrix)
	// OpenGL uses this one
	static constexpr float skyboxVertices[] = {
			-1.0f,  1.0f, -1.0f,  -1.0f, -1.0f, -1.0f,   1.0f, -1.0f, -1.0f,
			 1.0f, -1.0f, -1.0f,   1.0f,  1.0f, -1.0f,  -1.0f,  1.0f, -1.0f,

			-1.0f, -1.0f,  1.0f,  -1.0f, -1.0f, -1.0f,  -1.0f,  1.0f, -1.0f,
			-1.0f,  1.0f, -1.0f,  -1.0f,  1.0f,  1.0f,  -1.0f, -1.0f,  1.0f,

			 1.0f, -1.0f, -1.0f,   1.0f, -1.0f,  1.0f,   1.0f,  1.0f,  1.0f,
			 1.0f,  1.0f,  1.0f,   1.0f,  1.0f, -1.0f,   1.0f, -1.0f, -1.0f,

			-1.0f, -1.0f,  1.0f,  -1.0f,  1.0f,  1.0f,   1.0f,  1.0f,  1.0f,
			 1.0f,  1.0f,  1.0f,   1.0f, -1.0f,  1.0f,  -1.0f, -1.0f,  1.0f,

			-1.0f,  1.0f, -1.0f,   1.0f,  1.0f, -1.0f,   1.0f,  1.0f,  1.0f,
			 1.0f,  1.0f,  1.0f,  -1.0f,  1.0f,  1.0f,  -1.0f,  1.0f, -1.0f,

			-1.0f, -1.0f, -1.0f,  -1.0f, -1.0f,  1.0f,   1.0f, -1.0f, -1.0f,
			 1.0f, -1.0f, -1.0f,  -1.0f, -1.0f,  1.0f,   1.0f, -1.0f,  1.0f
	};

	// Hack to avoid back face culling by just forcing all the faces to be inwards
	static constexpr float skyboxVerticesInward[] = {
		// Back face
		 1.0f, -1.0f, -1.0f,  -1.0f, -1.0f, -1.0f,  -1.0f,  1.0f, -1.0f,
		-1.0f,  1.0f, -1.0f,   1.0f,  1.0f, -1.0f,   1.0f, -1.0f, -1.0f,

		// Left face
		-1.0f, -1.0f, -1.0f,  -1.0f, -1.0f,  1.0f,  -1.0f,  1.0f,  1.0f,
		-1.0f,  1.0f,  1.0f,  -1.0f,  1.0f, -1.0f,  -1.0f, -1.0f, -1.0f,

		// Right face
		1.0f, -1.0f,  1.0f,   1.0f, -1.0f, -1.0f,   1.0f,  1.0f, -1.0f,
		1.0f,  1.0f, -1.0f,   1.0f,  1.0f,  1.0f,   1.0f, -1.0f,  1.0f,

		// Front face
		-1.0f, -1.0f,  1.0f,   1.0f, -1.0f,  1.0f,   1.0f,  1.0f,  1.0f,
		1.0f,  1.0f,  1.0f,  -1.0f,  1.0f,  1.0f,  -1.0f, -1.0f,  1.0f,

		// Top face
		-1.0f,  1.0f, -1.0f,  -1.0f,  1.0f,  1.0f,   1.0f,  1.0f,  1.0f,
		1.0f,  1.0f,  1.0f,   1.0f,  1.0f, -1.0f,  -1.0f,  1.0f, -1.0f,

		// Bottom face
		-1.0f, -1.0f,  1.0f,  -1.0f, -1.0f, -1.0f,   1.0f, -1.0f, -1.0f,
		1.0f, -1.0f, -1.0f,   1.0f, -1.0f,  1.0f,  -1.0f, -1.0f,  1.0f
	};


	class CubeMap
	{

	public:

		CubeMap
		(
			const std::string& vertShader,
			const std::string& fragShader
		)
			: vertShader(vertShader), fragShader(fragShader)
		{}

		virtual ~CubeMap() = default;

		// Draw the cubemap for this frame, should be called as the last object to draw
		virtual void Render(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix) = 0;

		// This will change the current cubemap faces
		virtual void SetFaces(const std::array<std::shared_ptr<Texture2D>, 6>& faces) = 0;

		// Will take any regular texture, use equirectangular projection to turn it into a cubemap's faces, and then do the equivalent of SetFaces() with the output
		void FromEquirectangularProjection(const std::shared_ptr<Texture2D>& texture);

		// Set the order of the currently loaded faces, will rearrange them in real time, should cache the ordering as well if a cubemap hasn't been set yet
		void SetOrdering(const std::array<int, 6>& order);

		// In degrees for pitch yaw roll
		void SetRotation(const glm::vec3& rotation) { this->rotation = rotation; }
		const glm::vec3& GetRotation() const { return rotation; }

	protected:

		static void RotateImage180(unsigned char* data, int width, int height);

		// In degrees
		glm::vec3 rotation = { 0, 0, 0 };

		std::array<int, 6> faceOrder = { 0, 1, 2, 3, 4, 5 };
		std::array<std::shared_ptr<Texture2D>, 6> faces;

		// these shader paths are the one's specific to cubemap drawing
		std::string vertShader;
		std::string fragShader;

	};

}
