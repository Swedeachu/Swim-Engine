#include "PCH.h"
#include "CubeMapControlTest.h"
#include "Engine/Systems/Renderer/Core/Textures/TexturePool.h"

namespace Game
{

	static const std::string facesPath = "Cubemaps/Clean/cubemap";
	constexpr bool equirectangular = false;

	int CubeMapControlTest::Init()
	{
		// Turn on the sky
		std::unique_ptr<Engine::CubeMapController>& cubemapController = renderer->GetCubeMapController();
		if (!cubemapController) return 0;

		cubemapController->SetEnabled(true);

		Engine::TexturePool& texturePool = Engine::TexturePool::GetInstance();

		if constexpr (equirectangular)
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

		// cubemapController->SetOrdering({ 3, 1, 4, 5, 2, 0 }); // internally this is the default face ordering already

		return 0;
	}

	void CubeMapControlTest::Update(double dt)
	{
		// TODO: ability to mess with horizon level

		std::unique_ptr<Engine::CubeMapController>& cubemapController = renderer->GetCubeMapController();

		// Toggle on the sky with C key
		if (input->IsKeyTriggered('C'))
		{
			cubemapController->SetEnabled(!cubemapController->IsEnabled());
		}

		// Flip around the face ordering
		if (input->IsKeyTriggered('V'))
		{
			using Faces = std::array<int, 6>;
			flip = !flip;
			Faces order = flip ? Faces{ 0, 1, 2, 3, 4, 5 } : Faces{ 3, 1, 4, 5, 2, 0 };
			cubemapController->SetOrdering(order);
		}

		// Toggle the cubemap style
		if (input->IsKeyTriggered('X'))
		{
			Engine::TexturePool& texturePool = Engine::TexturePool::GetInstance();

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
	}

}