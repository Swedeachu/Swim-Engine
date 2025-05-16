#pragma once

#include <array>
#include <string>
#include <memory>
#include "Library/glm/glm.hpp"
#include "Engine/Systems/Renderer/Core/Textures/Texture2D.h"

namespace Engine
{

	class OpenGLCubeMap
	{

	public:

		OpenGLCubeMap
		(
			const std::array<std::shared_ptr<Texture2D>, 6>& faces,
			const std::string& vertShader, 
			const std::string& fragShader
		);
		
		~OpenGLCubeMap();

		void Render(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix);

	private:

		GLuint skyboxVAO = 0;
		GLuint skyboxVBO = 0;
		GLuint cubemapTexture = 0;
		GLuint shaderProgram = 0;

		// these shader paths are the one's specific to cubemap drawing in OpenGL
		std::string vertShader;
		std::string fragShader;

		GLuint LoadCubemap(const std::array<std::shared_ptr<Texture2D>, 6>& faces);
		void LoadSkyboxMesh();
		GLuint LoadSkyboxShader();

	};

}