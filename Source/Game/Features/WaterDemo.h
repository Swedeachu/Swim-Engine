#pragma once

#include "Engine/Systems/Renderer/Runtime/RenderFeature.h"

#include <array>

namespace Game
{

	// A rectangle of water drawn in the transparent pass with the engine's water shading
	// (Source/Shaders/Slang/Water/WaterShading.slang): small analytic waves for the
	// normal, refraction and absorption against the opaque scene copy.
	class WaterDemo : public Engine::RenderFeature
	{

	  public:

		std::string_view GetName() const override { return "WaterDemo"; }

		std::uint32_t GetGeometryPasses() const override;

		Engine::RenderGeometryResources PrepareGeometry(Engine::RenderFeatureContext& context) override;

		void DrawGeometry(Engine::RenderFeatureDrawContext& context) override;

		std::array<float, 3> Center{ 0.0f, 0.6f, 0.0f };
		std::array<float, 2> HalfSize{ 12.0f, 12.0f };
		float WaveHeight = 1.0f; // Scales the wave slopes.
		float Murk = 1.0f;		 // Scales the scattering.

	  private:

		float time = 0.0f;

	};

} // namespace Game
