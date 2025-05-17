#include "PCH.h"
#include "OpenGLCubeMap.h"
#include "Engine/SwimEngine.h"
#include "OpenGLRenderer.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "Library/stb/stb_image_resize2.h"

namespace Engine
{

	// Cube vertices for skybox (size doesn't matter, we scale it in shader/view matrix)
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

	OpenGLCubeMap::OpenGLCubeMap
	(
		const std::array<std::shared_ptr<Texture2D>, 6>& faces, 
		const std::string& vertShader, 
		const std::string& fragShader
	)
		: vertShader(vertShader), fragShader(fragShader)
	{
		LoadSkyboxMesh();
		cubemapTexture = LoadCubemap(faces);
		shaderProgram = LoadSkyboxShader();
	}

	OpenGLCubeMap::~OpenGLCubeMap()
	{
		glDeleteVertexArrays(1, &skyboxVAO);
		glDeleteBuffers(1, &skyboxVBO);
		glDeleteTextures(1, &cubemapTexture);
		glDeleteProgram(shaderProgram);
	}

	void OpenGLCubeMap::Render(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix)
	{
		glDepthFunc(GL_LEQUAL); // Draw skybox behind everything
		glUseProgram(shaderProgram);

		glm::mat4 viewNoTranslation = glm::mat4(glm::mat3(viewMatrix));
		glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, &viewNoTranslation[0][0]);
		glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, &projectionMatrix[0][0]);

		glBindVertexArray(skyboxVAO);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
		glDrawArrays(GL_TRIANGLES, 0, 36);
		glBindVertexArray(0);
		glDepthFunc(GL_LESS); // Restore default
	}

	static void RotateImage180(unsigned char* data, int width, int height)
	{
		const int channels = 4; // RGBA
		const int rowSize = width * channels;

		std::vector<unsigned char> temp(rowSize); // temp buffer for swapping

		for (int y = 0; y < height / 2; ++y)
		{
			unsigned char* rowTop = data + y * rowSize;
			unsigned char* rowBottom = data + (height - 1 - y) * rowSize;

			// Copy top row to temp
			std::memcpy(temp.data(), rowTop, rowSize);

			// Copy flipped bottom row to top
			for (int x = 0; x < width; ++x)
			{
				for (int c = 0; c < channels; ++c)
				{
					rowTop[x * channels + c] = rowBottom[(width - 1 - x) * channels + c];
				}
			}

			// Copy flipped temp to bottom
			for (int x = 0; x < width; ++x)
			{
				for (int c = 0; c < channels; ++c)
				{
					rowBottom[x * channels + c] = temp[(width - 1 - x) * channels + c];
				}
			}
		}

		// If height is odd, also flip the center row horizontally
		if (height % 2 == 1)
		{
			unsigned char* centerRow = data + (height / 2) * rowSize;
			for (int x = 0; x < width / 2; ++x)
			{
				for (int c = 0; c < channels; ++c)
				{
					std::swap(centerRow[x * channels + c], centerRow[(width - 1 - x) * channels + c]);
				}
			}
		}
	}

	GLuint OpenGLCubeMap::LoadCubemap(const std::array<std::shared_ptr<Texture2D>, 6>& faces)
	{
		GLuint textureID;
		glGenTextures(1, &textureID);
		glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

		// --- 1. Find the largest dimension among all faces ---
		uint32_t maxSize = 0;
		for (const auto& face : faces)
		{
			maxSize = std::max({ maxSize, face->GetWidth(), face->GetHeight() });
		}

		// --- 2. Compute largest POT <= maxSize ---
		auto LargestPowerOfTwoBelowOrEqual = [](uint32_t x) -> uint32_t
		{
			uint32_t pot = 1;
			while ((pot << 1) <= x) { pot <<= 1; }
			return pot;
		};
		uint32_t finalSize = LargestPowerOfTwoBelowOrEqual(maxSize);

		std::cout << "[CubeMap] Resizing all faces to " << finalSize << "x" << finalSize << std::endl;

		// --- 3. Resize each face to finalSize x finalSize using stb_image_resize2 ---
		for (GLuint i = 0; i < 6; ++i)
		{
			unsigned char* src = faces[i]->GetData();
			int srcW = static_cast<int>(faces[i]->GetWidth());
			int srcH = static_cast<int>(faces[i]->GetHeight());
			const int srcStride = srcW * 4;
			const int dstStride = finalSize * 4;

			std::vector<unsigned char> resizedPixels(finalSize * finalSize * 4);

			bool success = stbir_resize_uint8_linear(
				src, srcW, srcH, srcStride,
				resizedPixels.data(), finalSize, finalSize, dstStride,
				STBIR_RGBA
			);

			if (!success)
			{
				throw std::runtime_error("Failed to resize cubemap face " + std::to_string(i));
			}

			// Auto-rotate top (index 2) and bottom (index 3)
			if (i == 2 || i == 3)
			{
				RotateImage180(resizedPixels.data(), finalSize, finalSize);
			}

			glTexImage2D(
				GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
				0,
				GL_RGBA,
				finalSize,
				finalSize,
				0,
				GL_RGBA,
				GL_UNSIGNED_BYTE,
				resizedPixels.data()
			);
		}

		// --- 4. Enable mipmaps and filtering ---
		glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		// --- 5. Wrapping mode ---
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

		return textureID;
	}

	void OpenGLCubeMap::LoadSkyboxMesh()
	{
		glGenVertexArrays(1, &skyboxVAO);
		glGenBuffers(1, &skyboxVBO);
		glBindVertexArray(skyboxVAO);
		glBindBuffer(GL_ARRAY_BUFFER, skyboxVBO);
		glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), skyboxVertices, GL_STATIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
	}

	GLuint OpenGLCubeMap::LoadSkyboxShader()
	{
		std::shared_ptr<OpenGLRenderer> renderer = SwimEngine::GetInstance()->GetOpenGLRenderer();

		// Load basic skybox shaders
		std::string vsSrc = renderer->LoadTextFile(vertShader);
		std::string fsSrc = renderer->LoadTextFile(fragShader);

		GLuint vs = renderer->CompileGLSLShader(GL_VERTEX_SHADER, vsSrc.c_str());
		GLuint fs = renderer->CompileGLSLShader(GL_FRAGMENT_SHADER, fsSrc.c_str());

		return renderer->LinkShaderProgram({ vs, fs });
	}

	void OpenGLCubeMap::FromEquirectangularProjection(const std::shared_ptr<Texture2D>& texture)
	{
		// TODO (will take code from swim pack porter sky convert for this, we will probably have this be a base method in CubeMap actually to be consistent in logic)
	}

	void OpenGLCubeMap::SetFaces(const std::array<std::shared_ptr<Texture2D>, 6>& faces)
	{
		// TODO
	}

}
