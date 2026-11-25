#include "Game/Features/WaterDemo.h"

namespace Game
{

	namespace
	{

		struct WaterDemoConstants
		{
			std::array<float, 4> Center{};	// xyz, w: time.
			std::array<float, 4> Size{};	// xy: half size, z: wave scale, w: murk.
			std::array<float, 4> Viewport{}; // xy: pixels.
		};

	} // namespace

	std::uint32_t WaterDemo::GetGeometryPasses() const
	{
		return Engine::RenderGeometryPassBit(Engine::RenderGeometryPass::Transparent);
	}

	Engine::RenderGeometryResources WaterDemo::PrepareGeometry(Engine::RenderFeatureContext& context)
	{
		time = context.View().Time;
		Engine::RenderGeometryResources resources;
		resources.SceneCopy = true;
		return resources;
	}

	void WaterDemo::DrawGeometry(Engine::RenderFeatureDrawContext& context)
	{
		const auto color = context.SceneColor();
		const auto depth = context.SceneDepth();

		if (!color || !depth)
		{
			return;
		}

		WaterDemoConstants constants;
		constants.Center = { Center[0], Center[1], Center[2], time };
		constants.Size = { HalfSize[0], HalfSize[1], WaveHeight, Murk };
		constants.Viewport = { float(context.View().Width), float(context.View().Height), 0.0f, 0.0f };
		context.Program("WaterDemo")
			.Texture("SceneColor", *color)
			.Texture("SceneDepth", *depth)
			.Sampler("SceneSampler", "LinearClamp")
			.Constants(constants)
			.Draw(6);
	}

} // namespace Game
