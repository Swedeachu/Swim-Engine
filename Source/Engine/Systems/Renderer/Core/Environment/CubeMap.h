#pragma once 

#include "array"
#include "memory"
#include "Engine/Systems/Renderer/Core/Textures/Texture2D.h"

namespace Engine
{

	class CubeMap
	{

	public:

		// Draw the cubemap for this frame, should be called as the last object to draw
		virtual void Render(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix) = 0;

		// This will change the current cubemap faces
		virtual void SetFaces(const std::array<std::shared_ptr<Texture2D>, 6>& faces) = 0;

		// Will take any regular texture, use equirectangular projection to turn it into a cubemap's faces, and then do the equivalent of SetFaces() with the output
		virtual void FromEquirectangularProjection(const std::shared_ptr<Texture2D>& texture) = 0;

	};

}
