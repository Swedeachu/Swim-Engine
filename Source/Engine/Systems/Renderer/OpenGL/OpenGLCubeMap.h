#pragma once

#include "Engine/Systems/Renderer/Core/Environment/CubeMap.h"

namespace Engine
{

	class OpenGLCubeMap : public CubeMap
	{

	public:

		OpenGLCubeMap
		(
			const std::string& vertShader, 
			const std::string& fragShader
		);
		
		~OpenGLCubeMap();

		void Render(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix) override;

		void FromEquirectangularProjection(const std::shared_ptr<Texture2D>& texture) override;

		void SetFaces(const std::array<std::shared_ptr<Texture2D>, 6>& faces) override;

		void SetOrdering(const std::array<int, 6>& order) override;

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
		GLuint CreateCubemap(const std::array<std::shared_ptr<Texture2D>, 6>& faces);

		std::array<int, 6> faceOrder = { 0, 1, 2, 3, 4, 5 };
		std::array<std::shared_ptr<Texture2D>, 6> faces;

	};

}