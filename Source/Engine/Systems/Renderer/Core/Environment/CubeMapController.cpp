#include "PCH.h"
#include "CubeMapController.h"
#include "Engine/Systems/Renderer/Core/Textures/TexturePool.h"
#include "Engine/Systems/Renderer/OpenGL/OpenGLCubeMap.h"

namespace Engine
{

	CubeMapController::CubeMapController(const std::string& basePath, const std::string& vertPath, const std::string& fragPath)
	{
		std::array<std::shared_ptr<Texture2D>, 6> cubemapFaces;
		// const std::array<const char*, 6> suffixes = { "0", "1", "2", "3", "4", "5" };
		// Super hack to fit minecraft bedrock style, which is where I am sourcing the cubemap style from
		const std::array<const char*, 6> suffixes = { "3", "1", "4", "5", "2", "0" };

		TexturePool& pool = TexturePool::GetInstance();

		for (size_t i = 0; i < 6; ++i)
		{
			cubemapFaces[i] = pool.GetTexture2D(basePath + suffixes[i]);
		}

		// we need to make the correct cubemap type based on render context, which we then agonostically wrap over
		if constexpr (SwimEngine::CONTEXT == SwimEngine::RenderContext::OpenGL)
		{
			cubemap = std::make_unique<OpenGLCubeMap>(
				cubemapFaces,
				vertPath,
				fragPath
			);
		}
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

}
