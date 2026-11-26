#include "Engine/Systems/Renderer/ScreenSpace/DistanceFog.h"
#include "Engine/Systems/Renderer/ScreenSpace/ScreenSpaceReference.h"
#include "Engine/Systems/Renderer/ScreenSpace/ScreenSpaceSettings.h"
#include "Tests/Fixtures/ScreenSpaceFixture.h"
#include "Tests/Framework/Test.h"

#include <cmath>

using namespace Swim::Render;

namespace
{

	bool Near(float a, float b, float tolerance = 1.0e-4f)
	{
		return std::abs(a - b) <= tolerance;
	}

} // namespace

SWIM_TEST("Render.ScreenSpace.DistanceFog", "CurvesRunFromStartToEndAndCapAtMaxOpacity")
{
	using Engine::DistanceFogShape;
	using Engine::FogCurve;

	for (const auto curve : { FogCurve::Linear, FogCurve::Smooth, FogCurve::Power })
	{
		SWIM_CHECK(DistanceFogShape(curve, 100.0f, 300.0f, 0.0f, 3.0f, 50.0f) == 0.0f);
		SWIM_CHECK(DistanceFogShape(curve, 100.0f, 300.0f, 0.0f, 3.0f, 100.0f) == 0.0f);
		SWIM_CHECK(Near(DistanceFogShape(curve, 100.0f, 300.0f, 0.0f, 3.0f, 300.0f), 1.0f));
		SWIM_CHECK(Near(DistanceFogShape(curve, 100.0f, 300.0f, 0.0f, 3.0f, 900.0f), 1.0f));
	}

	SWIM_CHECK(Near(DistanceFogShape(FogCurve::Linear, 100.0f, 300.0f, 0.0f, 3.0f, 200.0f), 0.5f));
	SWIM_CHECK(Near(DistanceFogShape(FogCurve::Smooth, 100.0f, 300.0f, 0.0f, 3.0f, 200.0f), 0.5f));
	// Power keeps the near half nearly clear: light fog only at the far edge.
	SWIM_CHECK(Near(DistanceFogShape(FogCurve::Power, 100.0f, 300.0f, 0.0f, 3.0f, 200.0f), 0.125f));
	SWIM_CHECK(Near(DistanceFogShape(FogCurve::Exponential, 0.0f, 0.0f, 0.01f, 0.0f, 100.0f), 1.0f - std::exp(-1.0f)));

	Engine::DistanceFogSettings fog;
	fog.Curve = FogCurve::Linear;
	fog.StartDistance = 0.0f;
	fog.EndDistance = 100.0f;
	fog.MaxOpacity = 0.6f;
	SWIM_CHECK(fog.Amount(100.0f) == 0.0f); // Off.
	fog.Enabled = true;
	SWIM_CHECK(Near(fog.Amount(100.0f), 0.6f));
	const auto gpu = Engine::PackDistanceFog(fog);
	SWIM_CHECK(Near(gpu.ColorOpacity[3], 0.6f) && gpu.Curve == 1u && gpu.Range[1] == 100.0f);
	fog.Enabled = false;
	SWIM_CHECK(Engine::PackDistanceFog(fog).ColorOpacity[3] == 0.0f);
}

SWIM_TEST("Render.ScreenSpace.DistanceFog", "SceneFogFollowsTheConfiguredCurve")
{
	ScreenSpaceSettings settings;
	settings.Fog.Enabled = true;
	settings.Fog.Curve = FogCurve::Power;
	settings.Fog.StartDistance = 200.0f;
	settings.Fog.EndDistance = 400.0f;
	settings.Fog.Exponent = 2.0f;
	settings.Fog.MaxOpacity = 0.5f;
	const auto view = Swim::Testing::ScreenSpaceScene::View({ 3.0f, 2.0f, 6.0f }, { 1.0f, 0.0f, 0.0f }, 1.0f);
	const auto params = BuildScreenSpaceParams(settings, view, 16, 16, 0);
	const ScreenSpace::Float3 camera{ 0, 0, 0 };
	const ScreenSpace::Float3 direction{ 0, 0, -1 };
	SWIM_CHECK(Near(ScreenSpace::EvaluateFog(params, camera, direction, 150.0f).Transmittance, 1.0f));
	SWIM_CHECK(Near(ScreenSpace::EvaluateFog(params, camera, direction, 300.0f).Transmittance, 1.0f - 0.5f * 0.25f));
	SWIM_CHECK(Near(ScreenSpace::EvaluateFog(params, camera, direction, 1000.0f).Transmittance, 0.5f));

	// The exponential default is unchanged.
	settings.Fog = {};
	settings.Fog.Enabled = true;
	settings.Fog.HeightFalloff = 0.0f;
	const auto exponential = BuildScreenSpaceParams(settings, view, 16, 16, 0);
	SWIM_CHECK(Near(ScreenSpace::EvaluateFog(exponential, camera, direction, 50.0f).Transmittance, std::exp(-0.02f * 50.0f)));
}
