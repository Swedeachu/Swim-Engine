#pragma once

#include <glm/glm.hpp>

namespace Engine
{

	// A planar reflector on an entity with a MeshRenderer: the top layer of the reflection
	// hierarchy (planar -> screen space -> local probes -> environment). The renderer re-renders
	// the scene mirrored in the reflector's plane into a small capture whose size follows the
	// reflector's size on screen, shares one capture between coplanar faces, re-renders it only
	// when the camera or something in it moved enough (or it aged), and skips reflectors that
	// are off screen, back-facing, tiny or seen at angles where screen-space reflections
	// already show them. Glossy surfaces fade to the probes/environment by roughness.
	//
	//  - Box: the six faces of the unit cube mesh (mirror cubes; only faces facing the camera).
	//  - Plane: the local XZ plane, PlaneHalfExtents across (mirrors, glossy floors).
	//  - Sphere: the camera-facing cap of the unit sphere mesh (chrome balls), captured from the
	//    centre towards the camera; the rim keeps the object probe.
	struct PlanarReflector
	{
		enum class Shape
		{
			Box,
			Plane,
			Sphere,
		};

		bool Enabled = true;
		Shape Kind = Shape::Box;
		glm::vec2 PlaneHalfExtents{ 0.5f }; // Plane: local X/Z half extents.
		float Radius = 0.5f;				 // Sphere: local radius (the builtin sphere's).
		float Quality = 1.0f;				 // Resolution multiplier.
		float Priority = 1.0f;				 // Capture urgency multiplier.
	};

} // namespace Engine
