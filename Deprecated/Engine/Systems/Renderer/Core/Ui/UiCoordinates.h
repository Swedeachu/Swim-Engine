#pragma once

#include "Engine/Platform/PlatformTypes.h"
#include "Engine/Systems/Renderer/Renderer.h"
#include <glm/glm.hpp>

namespace Engine::UiCoordinates
{

	inline glm::vec2 WindowToVirtualCanvas(Swim::Platform::Float2 position, Swim::Platform::Extent2D logicalWindowSize)
	{
		if (logicalWindowSize.Width == 0 || logicalWindowSize.Height == 0)
		{
			return { position.X, position.Y };
		}

		const float scaleX = static_cast<float>(logicalWindowSize.Width) / Renderer::VirtualCanvasWidth;
		const float scaleY = static_cast<float>(logicalWindowSize.Height) / Renderer::VirtualCanvasHeight;

		return {
			position.X / scaleX,
			Renderer::VirtualCanvasHeight - (position.Y / scaleY)
		};
	}

}
