#include "PCH.h"
#include "CubeMapController.h"
#include "Engine/Systems/Renderer/Core/Textures/TexturePool.h"
#include "Engine/Systems/Renderer/OpenGL/OpenGLCubeMap.h"

namespace Engine
{

	// To construct this, pass in the directory for the first cubemap to load by default, and then the exact paths to the vertex and fragment shaders for the cubemap to use at render time.
	// All cubemap face textures are assume to be named cubemap_suffix, where suffix will be 0-5 for each face, such as cubemap_0, cubemap_1, etc.
	CubeMapController::CubeMapController(const std::string& vertPath, const std::string& fragPath)
	{
		if constexpr (SwimEngine::CONTEXT == SwimEngine::RenderContext::OpenGL)
		{
			cubemap = std::make_unique<OpenGLCubeMap>(
				vertPath,
				fragPath
			);
		}

		// Minecraft Bedrock style ordering since most our cubemaps will probably be of this, so we just set it by default out of the box like this
		if (cubemap) cubemap->SetOrdering({ 3, 1, 4, 5, 2, 0 });
	}

	void CubeMapController::Render(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix)
	{
		if (!enabled) return;

		if (cubemap) cubemap->Render(viewMatrix, projectionMatrix);
	}

	void CubeMapController::SetFaces(const std::array<std::shared_ptr<Texture2D>, 6>& faces)
	{
		if (cubemap) cubemap->SetFaces(faces);
	}

	void CubeMapController::FromEquirectangularProjection(const std::shared_ptr<Texture2D>& texture)
	{
		if (cubemap) cubemap->FromEquirectangularProjection(texture);
	}

	void CubeMapController::SetOrdering(const std::array<int, 6>& order)
	{
		// Check for uniqueness and valid range
		std::array<bool, 6> seen{ false, false, false, false, false, false };

		for (int i = 0; i < 6; ++i)
		{
			int val = order[i];
			if (val < 0 || val >= 6)
			{
				throw std::runtime_error("CubeMapController::SetOrdering: Invalid value in order array: " + std::to_string(val));
			}
			if (seen[val])
			{
				throw std::runtime_error("CubeMapController::SetOrdering: Duplicate value in order array: " + std::to_string(val));
			}
			seen[val] = true;
		}

		if (cubemap)
		{
			cubemap->SetOrdering(order);
		}
	}

}
