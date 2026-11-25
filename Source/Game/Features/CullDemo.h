#pragma once

#include "Engine/Systems/Renderer/Features/IndirectCuller.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Game
{

	// A grid of boxes behind a wall, culled by Engine::IndirectCuller (the camera's two
	// phases and every shadow tile) and drawn with DrawIndirectCount in the depth prepass,
	// opaque and shadow passes (Source/Game/Shaders/CullDemo.slang). The wall is drawn
	// directly as an occluder. With Xray on, the transparent pass draws the culler's camera
	// lists in front of everything (early green, late red): boxes hidden by the wall vanish.
	class CullDemo : public Engine::RenderFeature
	{

	  public:

		std::string_view GetName() const override { return "CullDemo"; }

		std::uint32_t GetGeometryPasses() const override;

		Engine::RenderGeometryResources PrepareGeometry(Engine::RenderFeatureContext& context) override;

		bool UsesOcclusion() const override { return true; }

		void CullGeometry(Engine::RenderFeatureContext& context, const Engine::RenderFeatureOcclusion& occlusion) override;

		void DrawGeometry(Engine::RenderFeatureDrawContext& context) override;

		std::array<float, 3> Origin{ -30.0f, 0.0f, 30.0f }; // The grid's near-left corner; the wall stands in front (-z).
		std::uint32_t Columns = 60;
		std::uint32_t Rows = 50;
		float Spacing = 0.6f;
		bool Wall = true;
		bool Xray = false;
		bool Occlusion = true;

	  private:

		Engine::IndirectCuller culler{ "Cull demo" };
		Swim::Render::GraphBuffer boxes{};
		std::uint32_t count = 0;
		std::vector<std::array<float, 12>> boxData;
		std::vector<std::array<float, 8>> boundsData;
		std::vector<Swim::Rhi::DrawIndirectCommand> commandData;
		std::array<float, 3> builtOrigin{};

		void Build();

	};

} // namespace Game
