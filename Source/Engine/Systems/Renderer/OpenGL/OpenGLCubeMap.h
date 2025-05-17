#pragma once

#include "Engine/Systems/Renderer/Core/Environment/CubeMap.h"

namespace Engine
{

	class OpenGLCubeMap : public CubeMap
	{

	public:

		OpenGLCubeMap
		(
			const std::array<std::shared_ptr<Texture2D>, 6>& faces,
			const std::string& vertShader, 
			const std::string& fragShader
		);
		
		~OpenGLCubeMap();

		void Render(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix) override;

		void FromEquirectangularProjection(const std::shared_ptr<Texture2D>& texture) override;

		void SetFaces(const std::array<std::shared_ptr<Texture2D>, 6>& faces) override;

	private:

		GLuint skyboxVAO = 0;
		GLuint skyboxVBO = 0;
		GLuint cubemapTexture = 0;
		GLuint shaderProgram = 0;

		// these shader paths are the one's specific to cubemap drawing in OpenGL
		std::string vertShader;
		std::string fragShader;

		void LoadSkyboxMesh();
		GLuint LoadSkyboxShader();
		GLuint LoadCubemap(const std::array<std::shared_ptr<Texture2D>, 6>& faces);

	};

}