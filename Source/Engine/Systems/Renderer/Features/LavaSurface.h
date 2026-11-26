#pragma once

#include "Engine/Systems/Renderer/Runtime/RenderFeature.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Engine
{

	// Molten surfaces drawn in the opaque pass from faces the game supplies (the same
	// flowing-liquid geometry as water): a smooth procedural melt, bright yellow-orange,
	// domain-warped noise flowing along the face with darker cooling streaks on it, gently
	// undulating normals, lit like the scene, with strong HDR emission for bloom. Heat
	// shimmer, embers, pops and the red light on the surroundings come from
	// Engine::VfxDirector. Program: LavaSurface.
	class LavaSurface final : public RenderFeature
	{

	  public:
		static constexpr std::uint32_t MaxQuads = 1u << 16;

		using Float3 = std::array<float, 3>;

		// A face of molten liquid. With UseCorners, the four Corners (counter-clockwise from the
		// front, drawn as triangles 0-1-2 and 0-2-3) are used as given, so flowing tops with
		// their own height per corner and trapezoid sides work like water geometry. Otherwise
		// the parallelogram Origin, Origin + AxisU, Origin + AxisU + AxisV, Origin + AxisV.
		// Seen from either side.
		struct Quad
		{
			Float3 Origin{ 0.0f, 0.0f, 0.0f };
			Float3 AxisU{ 1.0f, 0.0f, 0.0f };
			Float3 AxisV{ 0.0f, 0.0f, 1.0f };
			std::array<float, 2> Flow{ 0.0f, 0.0f }; // Surface flow along world x and z (m/s).
			float Heat = 1.0f;						 // 0..2: cooler (more crust) to hotter.
			std::array<Float3, 4> Corners{};
			bool UseCorners = false;
			bool Falling = false; // A side or falling face: the melt runs down at Look::FallSpeed.

			static Quad FromCorners(const std::array<Float3, 4>& corners, std::array<float, 2> flow = { 0.0f, 0.0f }, float heat = 1.0f)
			{
				Quad quad;
				quad.Corners = corners;
				quad.UseCorners = true;
				quad.Flow = flow;
				quad.Heat = heat;
				return quad;
			}

			// The four corners either way.
			std::array<Float3, 4> GetCorners() const;
		};

		struct Look
		{
			float Intensity = 0.8f;	  // Emission scale (HDR); about 1 is the tonemapper knee at EV 0.
			float Scale = 0.6f;		  // Flow pattern features per metre.
			float Crust = 0.18f;	  // How much cooler, darker skin drifts on the melt, 0..1.
			float ChurnSpeed = 1.0f;  // Pattern motion.
			float Pulse = 0.1f;		  // Heat throb, 0..1.
			float FallSpeed = 1.2f;	  // Falling faces (m/s).
			float WaveHeight = 0.04f; // Normal undulation (m).
			Float3 CrustColor{ 0.09f, 0.03f, 0.02f };
			float CrustRoughness = 0.7f;
			Float3 Tint{ 1.0f, 0.3f, 0.05f }; // Used with TintMix > 0.
			float TintMix = 0.0f;
		};

		std::vector<Quad> Quads; // Set by the game (or VfxDirector::SetLava); the first MaxQuads are drawn.
		Look Settings;

		std::string_view GetName() const override { return "Lava surface"; }

		std::uint32_t GetGeometryPasses() const override;

		RenderGeometryResources PrepareGeometry(RenderFeatureContext& context) override;

		void DrawGeometry(RenderFeatureDrawContext& context) override;

	  private:
		Swim::Render::GraphBuffer params{};
		Swim::Render::GraphBuffer quads{};
		std::uint32_t count = 0;
	};

} // namespace Engine
