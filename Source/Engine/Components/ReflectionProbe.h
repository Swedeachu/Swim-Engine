#pragma once

#include <glm/glm.hpp>

namespace Engine
{
	// A local reflection probe on an entity (the middle layer of the reflection hierarchy:
	// screen-space reflections, then probes, then the global environment). The renderer
	// captures the scene around the entity's world position (+ Offset) into a small cube map
	// a few faces per frame, prefilters it and stores the distance to what every texel sees,
	// so reflections of the floor, walls and nearby objects follow the real scene and correct
	// their parallax, including geometry that is behind the camera or off screen.
	//
	//  - Object probe (ObjectProbe = true, the default for a reflective object): captured from
	//    inside the entity without the entity itself (its render object is culled from the
	//    capture views) and used only by the entity's own pixels. Two chrome balls touching
	//    each see the other, the floor under them and the sky.
	//  - Area probe (ObjectProbe = false): serves every surface within InfluenceRadius (fading
	//    over BlendDistance), for glossy floors, walls and anything without its own probe.
	//
	// Dynamic probes are re-captured continuously within RenderSettings::ReflectionProbes
	// .FacesPerFrame (oldest faces first); static ones once, and again when they move.
	struct ReflectionProbe
	{
		bool Enabled = true;
		bool ObjectProbe = true;
		bool Dynamic = true;
		float InfluenceRadius = 4.0f;
		float BlendDistance = 1.0f;
		float CaptureNear = 0.05f; // Near plane of the captures (metres).
		float Priority = 1.0f;
		glm::vec3 Offset{ 0.0f };
	};
} // namespace Engine
