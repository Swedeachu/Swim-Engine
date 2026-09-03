#pragma once

#include "Engine/Platform/PlatformTypes.h"
#include "Engine/Systems/Renderer/Renderer.h"
#include <glm/glm.hpp>

namespace Engine::UiCoordinates
{

	inline glm::vec2 WindowToVirtualCanvas(const glm::vec2& position, Swim::Platform::Extent2D logicalWindowSize)
	{
		if (logicalWindowSize.Width == 0 || logicalWindowSize.Height == 0)
		{
			return position;
		}

		const float scaleX = static_cast<float>(logicalWindowSize.Width) / Renderer::VirtualCanvasWidth;
		const float scaleY = static_cast<float>(logicalWindowSize.Height) / Renderer::VirtualCanvasHeight;

		return {
			position.x / scaleX,
			Renderer::VirtualCanvasHeight - (position.y / scaleY)
		};
	}

}
