#include "PCH.h"
#include "CubeMapControlTest.h"
#include "Engine/Systems/Renderer/Core/Environment/CubeMapController.h"
#include "Engine/Systems/Renderer/Core/Textures/TexturePool.h"
#include "Engine/Systems/Scene/Scene.h"
#include "Engine/Input/InputSystem.h"
#include <array>
#include <memory>
#include <string>

namespace Game
{

	static const std::string facesPath = "Cubemaps/Clean/cubemap";
	constexpr bool equirectangular = false;

	int CubeMapControlTest::Init()
	{
		// Turn on the sky when this scene has presentation services.
		Engine::CubeMapController* cubemapController = scene->GetCubeMapController();
		if (!cubemapController)
		{
			return 0;
		}

		Engine::TexturePool& texturePool = scene->GetTexturePool();

		if constexpr (equirectangular)
		{
			// Convert one image to a cubemap with equirectangular projection
			std::shared_ptr<Engine::Texture2D> tex = texturePool.GetTexture2D("Sky/rect_sky");
			cubemapController->FromEquirectangularProjection(tex);
		}
		else
		{
			// Clean is the default six-face cubemap preset. Keep the CPU pixels until
			// SetFaces() has built the backend cubemap image.
			std::array<std::shared_ptr<Engine::Texture2D>, 6> faces = texturePool.GetTexturesContainingString<6>(facesPath);
			for (std::size_t index = 0; index < faces.size(); ++index)
			{
				if (!faces[index] || !faces[index]->GetData())
				{
					std::cerr << "[CubeMap] Clean preset is missing CPU face " << index
						<< " for lookup '" << facesPath << "'.\n";
					cubemapController->SetEnabled(false);
					return -1;
				}
			}
			cubemapController->SetFaces(faces);
			std::cout << "[CubeMap] Default preset: Clean (6 faces).\n";
		}

		cubemapController->SetEnabled(true);

		// cubemapController->SetOrdering({ 3, 1, 4, 5, 2, 0 }); // internally this is the default face ordering already

		return 0;
	}

	void CubeMapControlTest::Update(double dt)
	{
		// TODO: ability to mess with horizon level

		Engine::CubeMapController* cubemapController = scene->GetCubeMapController();

		if (!cubemapController)
		{
			return;
		}

		UpdateRotation(dt, cubemapController);

		// Toggle on the sky with C key
		if (input->IsKeyTriggered(Swim::Platform::KeyCode::C))
		{
			cubemapController->SetEnabled(!cubemapController->IsEnabled());
		}

		// Flip around the face ordering
		if (input->IsKeyTriggered(Swim::Platform::KeyCode::V))
		{
			using Faces = std::array<int, 6>;
			flip = !flip;
			Faces order = flip ? Faces{ 0, 1, 2, 3, 4, 5 } : Faces{ 3, 1, 4, 5, 2, 0 };
			cubemapController->SetOrdering(order);
		}

		// Toggle the cubemap style
		/* Abandoned feature for now
		if (input->IsKeyTriggered(Swim::Platform::KeyCode::X))
		{
			Engine::TexturePool& texturePool = scene->GetTexturePool();

			styleToggle = !styleToggle;

			if (styleToggle)
			{
				// Convert one image to a cubemap with equirectangular projection
				std::shared_ptr<Engine::Texture2D> tex = texturePool.GetTexture2D("Sky/rect_sky");
				cubemapController->FromEquirectangularProjection(tex);
			}
			else
			{
				// Get 6 seperate cubemap texture faces to supply
				std::array<std::shared_ptr<Engine::Texture2D>, 6> faces = texturePool.GetTexturesContainingString<6>(facesPath);
				cubemapController->SetFaces(faces);
			}
		}
		*/
	}

	void CubeMapControlTest::UpdateRotation(double dt, Engine::CubeMapController* cubemapController)
	{
		if (!cubemapController)
		{
			return;
		}

		Engine::CubeMap* cubemap = cubemapController->GetCubeMap();
		if (!cubemap)
		{
			return;
		}

		// Adjust rotation speed with F, T, H
		if (input->IsKeyDown(Swim::Platform::KeyCode::F))
		{
			rotationSpeed += 0.01f;
		}
		else if (input->IsKeyDown(Swim::Platform::KeyCode::T))
		{
			rotationSpeed -= 0.01f;
		}
		else if (input->IsKeyTriggered(Swim::Platform::KeyCode::H))
		{
			rotationSpeed = 0.5f;
		}

		// Get the current rotation from the cubemap
		glm::vec3 currentRotation = cubemap->GetRotation();

		// Calculate new rotation increment
		glm::vec3 deltaRotation = rotationDirection * rotationSpeed * static_cast<float>(dt);

		// Apply rotation
		glm::vec3 newRotation = currentRotation + deltaRotation;

		// Wrap each component to keep it in [0, 360)
		for (int i = 0; i < 3; ++i)
		{
			if (newRotation[i] >= 360.0f)
			{
				newRotation[i] -= 360.0f;
			}
			else if (newRotation[i] < 0.0f)
			{
				newRotation[i] += 360.0f;
			}
		}

		// Set the updated rotation back
		cubemap->SetRotation(newRotation);
	}

}
