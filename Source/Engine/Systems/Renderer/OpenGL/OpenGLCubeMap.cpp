#include "PCH.h"
#include "OpenGLCubeMap.h"
#include "Engine/SwimEngine.h"
#include "OpenGLRenderer.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "Library/stb/stb_image_resize2.h"

#define _USE_MATH_DEFINES
#include "math.h"

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

	// On construction just sets up the shaders and mesh, SetFaces is to be called by the gameplay scene code as needed
	OpenGLCubeMap::OpenGLCubeMap
	(
		const std::string& vertShader,
		const std::string& fragShader
	)
		: vertShader(vertShader), fragShader(fragShader)
	{
		LoadSkyboxMesh();
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

	GLuint OpenGLCubeMap::CreateCubemap(const std::array<std::shared_ptr<Texture2D>, 6>& faces)
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

	// This does not really work properly
	void OpenGLCubeMap::FromEquirectangularProjection(const std::shared_ptr<Texture2D>& texture)
	{
		if (!texture || !texture->GetData())
		{
			throw std::runtime_error("Invalid equirectangular texture provided");
		}

		const int srcWidth = static_cast<int>(texture->GetWidth());
		const int srcHeight = static_cast<int>(texture->GetHeight());
		const unsigned char* srcData = texture->GetData();

		// Determine face size - use a reasonable fraction of source height for good quality
		const int faceSize = std::max(256, srcHeight / 4);

		std::cout << "[CubeMap] Converting equirectangular " << srcWidth << "x" << srcHeight
			<< " to cubemap faces of size " << faceSize << "x" << faceSize << std::endl;

		// Create 6 face textures in memory
		std::array<std::shared_ptr<Texture2D>, 6> cubeFaces;

		// Face directions and up vectors for each cube face
		// Order: +X, -X, +Y, -Y, +Z, -Z
		const glm::vec3 faceDirections[6] = {
				glm::vec3(1.0f, 0.0f, 0.0f),   // +X (Right)
				glm::vec3(-1.0f, 0.0f, 0.0f),  // -X (Left)
				glm::vec3(0.0f, 1.0f, 0.0f),   // +Y (Top)
				glm::vec3(0.0f, -1.0f, 0.0f),  // -Y (Bottom)
				glm::vec3(0.0f, 0.0f, 1.0f),   // +Z (Front)
				glm::vec3(0.0f, 0.0f, -1.0f)   // -Z (Back)
		};

		const glm::vec3 faceUps[6] = {
				glm::vec3(0.0f, -1.0f, 0.0f),  // +X up
				glm::vec3(0.0f, -1.0f, 0.0f),  // -X up
				glm::vec3(0.0f, 0.0f, 1.0f),   // +Y up
				glm::vec3(0.0f, 0.0f, -1.0f),  // -Y up
				glm::vec3(0.0f, -1.0f, 0.0f),  // +Z up
				glm::vec3(0.0f, -1.0f, 0.0f)   // -Z up
		};

		// Generate each face
		for (int face = 0; face < 6; ++face)
		{
			std::vector<unsigned char> faceData(faceSize * faceSize * 4);

			const glm::vec3& forward = faceDirections[face];
			const glm::vec3& up = faceUps[face];
			const glm::vec3 right = glm::cross(forward, up);

			for (int y = 0; y < faceSize; ++y)
			{
				for (int x = 0; x < faceSize; ++x)
				{
					// Convert face pixel to normalized coordinates [-1, 1]
					const float u = (2.0f * x / (faceSize - 1)) - 1.0f;
					const float v = (2.0f * y / (faceSize - 1)) - 1.0f;

					// Calculate 3D direction vector for this pixel
					glm::vec3 direction = glm::normalize(forward + u * right + v * up);

					// Convert 3D direction to spherical coordinates
					const float theta = atan2f(direction.z, direction.x); // Azimuth
					const float phi = asinf(direction.y);                 // Elevation

					// Convert spherical to equirectangular UV coordinates
					const float equiU = (theta + M_PI) / (2.0f * M_PI);
					const float equiV = (phi + M_PI * 0.5f) / M_PI;

					// Clamp to valid range
					const float clampedU = std::max(0.0f, std::min(1.0f, equiU));
					const float clampedV = std::max(0.0f, std::min(1.0f, equiV));

					// Sample from equirectangular texture using bilinear interpolation
					const float srcX = clampedU * (srcWidth - 1);
					const float srcY = clampedV * (srcHeight - 1);

					const int x0 = static_cast<int>(floorf(srcX));
					const int y0 = static_cast<int>(floorf(srcY));
					const int x1 = std::min(x0 + 1, srcWidth - 1);
					const int y1 = std::min(y0 + 1, srcHeight - 1);

					const float fx = srcX - x0;
					const float fy = srcY - y0;

					// Get four neighboring pixels
					const int idx00 = (y0 * srcWidth + x0) * 4;
					const int idx01 = (y0 * srcWidth + x1) * 4;
					const int idx10 = (y1 * srcWidth + x0) * 4;
					const int idx11 = (y1 * srcWidth + x1) * 4;

					// Bilinear interpolation for each channel
					const int dstIdx = (y * faceSize + x) * 4;
					for (int c = 0; c < 4; ++c)
					{
						const float p00 = srcData[idx00 + c];
						const float p01 = srcData[idx01 + c];
						const float p10 = srcData[idx10 + c];
						const float p11 = srcData[idx11 + c];

						const float lerp1 = p00 * (1.0f - fx) + p01 * fx;
						const float lerp2 = p10 * (1.0f - fx) + p11 * fx;
						const float final = lerp1 * (1.0f - fy) + lerp2 * fy;

						faceData[dstIdx + c] = static_cast<unsigned char>(std::round(final));
					}
				}
			}

			// Create texture from face data
			cubeFaces[face] = std::make_shared<Texture2D>(faceSize, faceSize, faceData.data());
		}

		// Use existing SetFaces method to apply the generated faces with proper ordering
		SetFaces(cubeFaces);
	}

	void OpenGLCubeMap::SetFaces(const std::array<std::shared_ptr<Texture2D>, 6>& newFaces)
	{
		// Cache original faces
		faces = newFaces;

		// Apply faceOrder to determine which actual textures to bind in each slot
		std::array<std::shared_ptr<Texture2D>, 6> orderedFaces;
		for (int i = 0; i < 6; ++i)
		{
			orderedFaces[i] = faces[faceOrder[i]];
		}

		// Destroy old GPU texture if it exists
		if (cubemapTexture)
		{
			glDeleteTextures(1, &cubemapTexture);
		}

		cubemapTexture = CreateCubemap(orderedFaces);
	}

	void OpenGLCubeMap::SetOrdering(const std::array<int, 6>& order)
	{
		// Cache the new order
		faceOrder = order;

		// If a cubemap is already set, reapply with new order
		if (faces[0])
		{
			SetFaces(faces);
		}
	}

}
