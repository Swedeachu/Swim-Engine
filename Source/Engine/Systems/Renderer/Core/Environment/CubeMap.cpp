#include "PCH.h"
#include "CubeMap.h"

#define _USE_MATH_DEFINES
#include "math.h"

namespace Engine
{

	void CubeMap::SetOrdering(const std::array<int, 6>& order)
	{
		// Cache the new order
		faceOrder = order;

		// If a cubemap is already set, reapply with new order
		if (faces[0])
		{
			SetFaces(faces);
		}
	}

	void CubeMap::FromEquirectangularProjection(const std::shared_ptr<Texture2D>& texture)
	{
		if (!texture || !texture->GetData())
		{
			throw std::runtime_error("Invalid equirectangular texture provided");
		}

		const int srcWidth = static_cast<int>(texture->GetWidth());
		const int srcHeight = static_cast<int>(texture->GetHeight());
		const unsigned char* srcData = texture->GetData();

		// Determine face resolution
		const int faceSize = std::max(256, srcHeight / 4);

		std::cout << "[CubeMap] Converting equirectangular " << srcWidth << "x" << srcHeight
			<< " to cubemap faces of size " << faceSize << "x" << faceSize << std::endl;

		// Holds pixel data for each face (RGBA)
		std::array<std::vector<unsigned char>, 6> facePixels;
		for (int i = 0; i < 6; ++i)
		{
			facePixels[i].resize(faceSize * faceSize * 4);
		}

		// Cube face directions
		auto getDirection = [](int face, float u, float v) -> glm::vec3
		{
			switch (face)
			{
				case 0: return glm::normalize(glm::vec3(1.0f, -v, -u));   // +X
				case 1: return glm::normalize(glm::vec3(-1.0f, -v, u));   // -X
				case 2: return glm::normalize(glm::vec3(u, -1.0f, v));    // +Y
				case 3: return glm::normalize(glm::vec3(u, 1.0f, -v));    // -Y
				case 4: return glm::normalize(glm::vec3(u, -v, 1.0f));    // +Z
				case 5: return glm::normalize(glm::vec3(-u, -v, -1.0f));  // -Z
				default: return glm::vec3(0.0f);
			}
		};

		// Sampling function
		auto sampleEquirect = [&](float lon, float lat) -> glm::vec4
		{
			float u = lon / (2.0f * static_cast<float>(M_PI));
			float v = lat / static_cast<float>(M_PI);
			u = glm::clamp(u, 0.0f, 1.0f);
			v = glm::clamp(v, 0.0f, 1.0f);

			int x = static_cast<int>(u * (srcWidth - 1));
			int y = static_cast<int>(v * (srcHeight - 1));
			int idx = (y * srcWidth + x) * 4;

			return glm::vec4(
				srcData[idx + 0] / 255.0f,
				srcData[idx + 1] / 255.0f,
				srcData[idx + 2] / 255.0f,
				srcData[idx + 3] / 255.0f
			);
		};

		// Fill cubemap faces
		for (int face = 0; face < 6; ++face)
		{
			std::vector<unsigned char>& pixels = facePixels[face];

			for (int y = 0; y < faceSize; ++y)
			{
				for (int x = 0; x < faceSize; ++x)
				{
					// Map texel coord to [-1,1] range
					float u = 2.0f * (x + 0.5f) / faceSize - 1.0f;
					float v = 2.0f * (y + 0.5f) / faceSize - 1.0f;

					// Flip v to match OpenGL image coord system
					v = -v;

					glm::vec3 dir = getDirection(face, u, v);
					float theta = std::acos(glm::clamp(dir.y, -1.0f, 1.0f));  // [0, pi]
					float phi = std::atan2(dir.z, dir.x);                     // [-pi, pi]

					if (phi < 0.0f) { phi += 2.0f * static_cast<float>(M_PI); }

					glm::vec4 color = sampleEquirect(phi, theta);

					int index = (y * faceSize + x) * 4;
					pixels[index + 0] = static_cast<unsigned char>(glm::clamp(color.r * 255.0f, 0.0f, 255.0f));
					pixels[index + 1] = static_cast<unsigned char>(glm::clamp(color.g * 255.0f, 0.0f, 255.0f));
					pixels[index + 2] = static_cast<unsigned char>(glm::clamp(color.b * 255.0f, 0.0f, 255.0f));
					pixels[index + 3] = static_cast<unsigned char>(glm::clamp(color.a * 255.0f, 0.0f, 255.0f));
				}
			}
		}

		// Build Texture2D objects from the face data
		std::array<std::shared_ptr<Texture2D>, 6> cubeFaces;
		for (int i = 0; i < 6; ++i)
		{
			cubeFaces[i] = std::make_shared<Texture2D>(
				static_cast<uint32_t>(faceSize),
				static_cast<uint32_t>(faceSize),
				facePixels[i].data()
			);
		}

		// Set the cubemap faces
		SetFaces(cubeFaces);
	}

	void CubeMap::RotateImage180(unsigned char* data, int width, int height)
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

}