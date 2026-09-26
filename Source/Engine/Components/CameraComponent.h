#pragma once

#include <cstdint>

namespace Engine
{
	// Makes an entity a camera (Phase 23). Each frame the engine takes the active camera
	// with the highest Priority, if any, and puts the main camera at its entity's world
	// transform (looking down local -Z) with these lens settings; without one the main
	// camera stays wherever its controller (the FlyCameraController) leaves it.
	struct CameraComponent
	{
		float FieldOfView = 60.0f; // Vertical, degrees.
		float Near = 0.1f;
		float Far = 500.0f;
		std::int32_t Priority = 0;
		bool Active = true;
	};
} // namespace Engine
