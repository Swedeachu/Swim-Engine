#pragma once

#include "CubeMap.h"

namespace Engine
{

	class CubeMapController
	{

	public:

		CubeMapController(const std::string& basePath, const std::string& vertPath, const std::string& fragPath);

		// Draw the cubemap for this frame, should be called as the last object to draw
		void Render(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix);

		// This will change the current cubemap faces
		void SetFaces(const std::array<std::shared_ptr<Texture2D>, 6>& faces);

		// Will take any regular texture, use equirectangular projection to turn it into a cubemap's faces, and then do the equivalent of SetFaces() with the output
		void FromEquirectangularProjection(const std::shared_ptr<Texture2D>& texture);

		// If it should render each frame or not
		const bool IsEnabled() const { return enabled; }
		void SetEnabled(bool value) { enabled = value; }

	private:

		std::unique_ptr<CubeMap> cubemap;

		bool enabled{ true };

	};

}