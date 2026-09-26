#pragma once

#include "Engine/Systems/Camera/Camera.h"

#include <cstdint>

namespace Engine
{
	// Owns the main camera that the renderer draws the active scene from. Camera
	// controllers (FlyCameraController, gameplay cameras) move it; the engine keeps its
	// aspect in sync with the render surface. A camera cut (teleport, scene change)
	// tells the renderer to drop temporal history (TAA, occlusion, exposure adaptation).
	class CameraSystem
	{
	  public:
		Camera& GetCamera() { return camera; }

		const Camera& GetCamera() const { return camera; }

		void SetSurfaceSize(std::uint32_t width, std::uint32_t height);

		std::uint32_t GetSurfaceWidth() const { return surfaceWidth; }

		std::uint32_t GetSurfaceHeight() const { return surfaceHeight; }

		// Ray through a pixel of the render surface (top-left origin).
		CameraRay ScreenPointToRay(float x, float y) const;

		void RequestCameraCut() { cut = true; }

		// Returns and clears the pending cut.
		bool ConsumeCameraCut();

	  private:
		Camera camera;
		std::uint32_t surfaceWidth = 1280;
		std::uint32_t surfaceHeight = 720;
		bool cut = true; // The first frame is a cut.
	};
} // namespace Engine
