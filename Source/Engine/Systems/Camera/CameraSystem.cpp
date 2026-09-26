#include "Engine/Systems/Camera/CameraSystem.h"

namespace Engine
{
	void CameraSystem::SetSurfaceSize(std::uint32_t width, std::uint32_t height)
	{
		if (width == 0 || height == 0)
		{
			return; // Minimized: keep the last aspect.
		}
		surfaceWidth = width;
		surfaceHeight = height;
		camera.SetAspect(static_cast<float>(width) / static_cast<float>(height));
	}

	CameraRay CameraSystem::ScreenPointToRay(float x, float y) const
	{
		return camera.ScreenPointToRay(x, y, static_cast<float>(surfaceWidth), static_cast<float>(surfaceHeight));
	}

	bool CameraSystem::ConsumeCameraCut()
	{
		const bool value = cut;
		cut = false;
		return value;
	}
} // namespace Engine
