#pragma once

#include "Engine/Systems/Renderer/Runtime/RenderFeature.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Engine
{

	// Crisp screen-space outlines of highlighted objects, with an optional fresnel rim glow
	// on their surface. Each object is a triangle mesh and/or a set of boxes in its own local
	// space, placed by a transform. Every frame the objects' screen rectangle is ray-cast on
	// the GPU (exact silhouettes, no geometry pass) into a small mask of object, normal and
	// visibility, and the composite draws an anti-aliased outer outline of the given pixel
	// width around each silhouette, optional crease lines where the surface folds (the edges
	// of a box), and the rim glow. Parts behind the scene's depth can still be outlined
	// faintly (HiddenOpacity). Runs after tone mapping, so colours are exactly what is given.
	// The cost scales with the screen area the objects cover times their triangle count:
	// meant for a few objects of up to a few hundred triangles (a selection), not crowds.
	// Programs: OutlineMask, OutlineComposite.
	class Outline final : public RenderFeature
	{

	  public:

		struct Box
		{
			std::array<float, 3> Min{ 0.0f, 0.0f, 0.0f };
			std::array<float, 3> Max{ 1.0f, 1.0f, 1.0f };
		};

		struct Object
		{
			std::vector<std::array<float, 3>> Triangles; // Local-space positions, three per triangle.
			std::vector<Box> Boxes;						 // Local-space boxes.
			// Local to world, row-major 3x4 (world = M * float4(local, 1)): rotation, scale and
			// translation.
			std::array<float, 12> Transform{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
			std::array<float, 4> Color{ 0.0f, 0.0f, 0.0f, 0.8f }; // Display-referred sRGB colour, alpha = opacity.
			float Width = 2.0f;									  // Outline width in pixels (0..8; 0 = none).
			bool Creases = false;								  // Also draw lines where faces meet at an angle.
			float CreaseAngle = 30.0f;							  // Degrees between faces that makes a crease.
			// The fresnel rim glow on the object's surface: colour (rgb), strength (a, 0 = off) and
			// how tightly it hugs the silhouette.
			std::array<float, 4> Rim{ 1.0f, 1.0f, 1.0f, 0.0f };
			float RimPower = 3.0f;
			float HiddenOpacity = 0.0f; // Outline opacity where the object is behind scene geometry (0..1).
		};

		// Adds or replaces an object; returns its handle.
		std::uint32_t Set(const Object& object, std::uint32_t handle = 0);

		// Moves an object without re-sending its shape.
		void SetTransform(std::uint32_t handle, const std::array<float, 12>& transform);

		void Remove(std::uint32_t handle);

		void Clear();

		std::string_view GetName() const override { return "Outline"; }

		RenderFeatureStage GetStage() const override { return RenderFeatureStage::AfterPostProcess; }

		void Record(RenderFeatureContext& context) override;

	  private:

		struct Entry
		{
			std::uint32_t Handle = 0;
			Object Shape;
		};

		std::vector<Entry> objects;
		std::uint32_t nextHandle = 1;

	};

} // namespace Engine
