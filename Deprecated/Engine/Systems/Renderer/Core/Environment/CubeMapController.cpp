#include "PCH.h"
#include "CubeMapController.h"
#include <utility>

namespace Engine
{

	CubeMapController::CubeMapController(std::unique_ptr<CubeMap> cubemap)
		: cubemap(std::move(cubemap))
	{
		// Minecraft Bedrock style ordering since most our cubemaps will probably be of this, so we just set it by default out of the box like this
		if (this->cubemap)
		{
			this->cubemap->SetOrdering({ 3, 1, 4, 5, 2, 0 });
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
