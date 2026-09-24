#include "Engine/Systems/Renderer/Lights/LightDesc.h"
#include "Engine/Systems/Renderer/Lights/LightMath.h"
#include "Engine/Systems/Renderer/Shadows/ShadowPlanner.h"
#include "Engine/Systems/Renderer/Visibility/GpuViewRecord.h"
#include "Tests/Fixtures/ClusterFixture.h"
#include "Tests/Framework/Test.h"

#include <cmath>
#include <stdexcept>
#include <vector>

using namespace Swim;
using namespace Swim::Render;
namespace Sh = Swim::Render::Shadows;
namespace Scene = Swim::Testing::ClusterScene;

namespace
{
	Sh::ShadowCamera Camera()
	{
		Sh::ShadowCamera camera;
		camera.View = Scene::LookAt({ 0, 3, 10 }, { 0, 0, 0 }, { 0, 1, 0 });
		camera.VerticalFov = 0.9f;
		camera.Aspect = 16.0f / 9.0f;
		camera.Near = 0.1f;
		return camera;
	}

	ShadowCasterDesc Caster(LightType type, std::uint32_t slot, float priority, std::uint32_t resolution = 0)
	{
		LightDesc desc;
		desc.Type = type;
		desc.Position = { float(slot), 4, 0 };
		desc.Direction = type == LightType::Directional ? std::array<float, 3>{ 0.3f, -1, 0.2f } : std::array<float, 3>{ 0, -1, 0 };
		desc.Range = 20.0f;
		desc.OuterConeAngle = 0.6f;
		desc.ShadowIndex = slot;
		desc.Flags = LightFlags::CastsShadows;
		ShadowCasterDesc caster;
		caster.Slot = slot;
		caster.Light = Lights::EncodeLight(desc);
		caster.Priority = priority;
		caster.Resolution = resolution;
		return caster;
	}

	bool TilesDisjoint(const ShadowPlan& plan)
	{
		for (std::size_t i = 0; i < plan.Draws.size(); ++i)
		{
			for (std::size_t j = i + 1; j < plan.Draws.size(); ++j)
			{
				const auto& a = plan.Draws[i].Tile;
				const auto& b = plan.Draws[j].Tile;
				if (a.X < b.X + b.Size && b.X < a.X + a.Size && a.Y < b.Y + b.Size && b.Y < a.Y + a.Size)
				{
					return false;
				}
			}
		}
		return true;
	}
} // namespace

SWIM_TEST("Render.Shadows.Planner", "BuildsCascadeSpotAndPointRecordsAndViews")
{
	ShadowSettings settings;
	ShadowAtlasAllocator atlas(settings.AtlasSize, settings.MinTile);
	const std::vector<ShadowCasterDesc> casters{
		Caster(LightType::Point, 5, 1.0f),
		Caster(LightType::Directional, 0, 10.0f),
		Caster(LightType::Spot, 2, 2.0f),
	};
	const auto plan = PlanShadows(settings, Camera(), casters, atlas);
	SWIM_CHECK_EQUAL(plan.AtlasSize, settings.AtlasSize);
	SWIM_CHECK_EQUAL(plan.Records.size(), std::size_t(settings.MaxSlots));
	SWIM_REQUIRE_EQUAL(plan.Views.size(), std::size_t(3 + 1 + 6));
	SWIM_CHECK_EQUAL(plan.Draws.size(), plan.Views.size());
	SWIM_CHECK(TilesDisjoint(plan));

	// Slot order: directional (0), spot (2), point (5).
	const auto& sun = plan.Records[0];
	SWIM_CHECK_EQUAL(sun.Kind, std::uint32_t(ShadowKind::Directional));
	SWIM_CHECK_EQUAL(sun.FirstView, 0u);
	SWIM_CHECK_EQUAL(sun.ViewCount, 3u);
	SWIM_CHECK(sun.CascadeFar[0] > 0.1f && sun.CascadeFar[0] < sun.CascadeFar[1] && sun.CascadeFar[1] < sun.CascadeFar[2]);
	SWIM_CHECK(std::abs(sun.CascadeFar[2] - settings.Cascades.MaxDistance) < 1.0e-4f && sun.CascadeFar[3] == 0.0f);
	SWIM_CHECK_EQUAL(sun.PcfRadius, settings.PcfRadius);
	SWIM_CHECK(sun.NormalBias == settings.NormalBias && sun.SlopeBias == settings.SlopeBias && sun.DepthBias == settings.DepthBias);

	const auto& spot = plan.Records[2];
	SWIM_CHECK_EQUAL(spot.Kind, std::uint32_t(ShadowKind::Spot));
	SWIM_CHECK_EQUAL(spot.FirstView, 3u);
	SWIM_CHECK_EQUAL(spot.ViewCount, 1u);
	const auto& point = plan.Records[5];
	SWIM_CHECK_EQUAL(point.Kind, std::uint32_t(ShadowKind::Point));
	SWIM_CHECK_EQUAL(point.FirstView, 4u);
	SWIM_CHECK_EQUAL(point.ViewCount, 6u);
	SWIM_CHECK(point.LightPosition[0] == 5.0f && point.LightPosition[1] == 4.0f && point.LightPosition[2] == 0.0f);
	for (std::uint32_t slot : { 1u, 3u, 4u, 6u, 63u })
	{
		SWIM_CHECK_EQUAL(plan.Records[slot].Kind, std::uint32_t(ShadowKind::None));
	}

	for (std::size_t i = 0; i < plan.Draws.size(); ++i)
	{
		const auto& draw = plan.Draws[i];
		const auto& view = plan.Views[i];
		SWIM_CHECK(std::equal(std::begin(view.ViewProjection), std::end(view.ViewProjection), std::begin(draw.View.ViewProjection)));
		SWIM_CHECK(view.AtlasRect[0] == float(draw.Tile.X) && view.AtlasRect[1] == float(draw.Tile.Y));
		SWIM_CHECK(view.AtlasRect[2] == float(draw.Tile.Size) && view.AtlasRect[3] == float(draw.Tile.Size));
		SWIM_CHECK(view.TexelWorldSize > 0.0f);
		SWIM_CHECK(
			std::equal(draw.Visibility.ViewProjection.begin(), draw.Visibility.ViewProjection.end(), std::begin(view.ViewProjection)));
		SWIM_CHECK_EQUAL(draw.Visibility.Flags, std::uint32_t(GpuViewFlags::ShadowCasters | GpuViewFlags::ResetLodHistory));
	}
	// Default resolutions per kind; the kinds' projections.
	SWIM_CHECK(plan.Draws[0].Tile.Size == settings.CascadeResolution && plan.Views[0].Perspective == 0u);
	SWIM_CHECK(plan.Draws[3].Tile.Size == settings.SpotResolution && plan.Views[3].Perspective == 1u);
	SWIM_CHECK(plan.Draws[4].Tile.Size == settings.PointResolution && plan.Views[4].Perspective == 1u);
	SWIM_CHECK(std::abs(plan.Views[4].TexelWorldSize - 2.0f / float(settings.PointResolution)) < 1.0e-7f);
	const float spotFov = Sh::SpotShadowFov(casters[2].Light);
	SWIM_CHECK(std::abs(plan.Views[3].TexelWorldSize - 2.0f * std::tan(spotFov * 0.5f) / float(settings.SpotResolution)) < 1.0e-7f);
	for (std::uint32_t face = 0; face < 6; ++face)
	{
		SWIM_CHECK_EQUAL(plan.Draws[4 + face].Index, face);
		SWIM_CHECK_EQUAL(plan.Draws[4 + face].Slot, 5u);
		SWIM_CHECK(plan.Draws[4 + face].Kind == ShadowKind::Point);
	}

	const auto& stats = plan.Stats;
	SWIM_CHECK_EQUAL(stats.Casters, 3u);
	SWIM_CHECK_EQUAL(stats.Placed, 3u);
	SWIM_CHECK_EQUAL(stats.Views, 10u);
	SWIM_CHECK_EQUAL(stats.OverBudget + stats.Evicted + stats.Downgraded + stats.Reused, 0u);

	// The next frame reuses every tile.
	const auto again = PlanShadows(settings, Camera(), casters, atlas);
	SWIM_CHECK_EQUAL(again.Stats.Reused, 3u);
	for (std::size_t i = 0; i < plan.Draws.size(); ++i)
	{
		SWIM_CHECK(again.Draws[i].Tile == plan.Draws[i].Tile);
	}
}

SWIM_TEST("Render.Shadows.Planner", "BudgetsDropTheLowestPriorityCastersOfEachKind")
{
	ShadowSettings settings;
	settings.MaxPointShadows = 1;
	settings.MaxSpotShadows = 2;
	ShadowAtlasAllocator atlas(settings.AtlasSize, settings.MinTile);
	const std::vector<ShadowCasterDesc> casters{
		Caster(LightType::Point, 1, 1.0f), Caster(LightType::Point, 2, 3.0f), // Kept.
		Caster(LightType::Spot, 3, 1.0f),									  // Dropped (lowest spot).
		Caster(LightType::Spot, 4, 2.0f), Caster(LightType::Spot, 5, 5.0f), Caster(LightType::Directional, 6, 0.0f),
		Caster(LightType::Directional, 7, 0.5f), // Kept (one directional).
	};
	const auto plan = PlanShadows(settings, Camera(), casters, atlas);
	SWIM_CHECK_EQUAL(plan.Stats.OverBudget, 3u);
	SWIM_CHECK_EQUAL(plan.Records[1].Kind, std::uint32_t(ShadowKind::None));
	SWIM_CHECK_EQUAL(plan.Records[2].Kind, std::uint32_t(ShadowKind::Point));
	SWIM_CHECK_EQUAL(plan.Records[3].Kind, std::uint32_t(ShadowKind::None));
	SWIM_CHECK_EQUAL(plan.Records[4].Kind, std::uint32_t(ShadowKind::Spot));
	SWIM_CHECK_EQUAL(plan.Records[5].Kind, std::uint32_t(ShadowKind::Spot));
	SWIM_CHECK_EQUAL(plan.Records[6].Kind, std::uint32_t(ShadowKind::None));
	SWIM_CHECK_EQUAL(plan.Records[7].Kind, std::uint32_t(ShadowKind::Directional));
	SWIM_CHECK_EQUAL(plan.Views.size(), std::size_t(6 + 2 + 3));
	SWIM_CHECK(TilesDisjoint(plan));
}

SWIM_TEST("Render.Shadows.Planner", "SmallAtlasesDowngradeAndEvictToUnshadowed")
{
	ShadowSettings settings;
	settings.AtlasSize = 1024;
	settings.MinTile = 256;
	settings.MaxSpotShadows = 64;
	ShadowAtlasAllocator atlas(settings.AtlasSize, settings.MinTile);
	std::vector<ShadowCasterDesc> casters;
	for (std::uint32_t slot = 0; slot < 20; ++slot)
	{
		casters.push_back(Caster(LightType::Spot, slot, float(slot), slot == 16 ? 256 : 512));
	}
	const auto plan = PlanShadows(settings, Camera(), casters, atlas);
	// Slots 19-17 take three 512 quadrants, slot 16 asks for a 256 in the last one,
	// slots 15-13 are downgraded to its other three 256 cells and the rest are evicted.
	std::uint32_t shadowed = 0;
	for (const auto& record : plan.Records)
	{
		shadowed += record.Kind != std::uint32_t(ShadowKind::None) ? 1u : 0u;
	}
	SWIM_CHECK_EQUAL(shadowed, plan.Stats.Placed);
	SWIM_CHECK_EQUAL(plan.Stats.Placed + plan.Stats.Evicted, 20u);
	SWIM_CHECK_EQUAL(plan.Stats.Placed, 7u);
	SWIM_CHECK_EQUAL(plan.Stats.Downgraded, 3u);
	SWIM_CHECK_EQUAL(plan.Stats.Evicted, 13u);
	SWIM_CHECK_EQUAL(plan.Records[13].Kind, std::uint32_t(ShadowKind::Spot));
	SWIM_CHECK_EQUAL(plan.Records[12].Kind, std::uint32_t(ShadowKind::None));
	SWIM_CHECK_EQUAL(plan.Records[19].Kind, std::uint32_t(ShadowKind::Spot));
	SWIM_CHECK_EQUAL(plan.Records[0].Kind, std::uint32_t(ShadowKind::None));
	SWIM_CHECK(TilesDisjoint(plan));
	std::uint64_t texels = 0;
	for (const auto& draw : plan.Draws)
	{
		texels += std::uint64_t(draw.Tile.Size) * draw.Tile.Size;
	}
	SWIM_CHECK_EQUAL(texels, std::uint64_t(1024) * 1024);
	// Evicted records own no views; placed ones index their own.
	for (std::uint32_t slot = 0; slot < 20; ++slot)
	{
		const auto& record = plan.Records[slot];
		if (record.Kind == std::uint32_t(ShadowKind::None))
		{
			SWIM_CHECK_EQUAL(record.ViewCount, 0u);
			continue;
		}
		SWIM_REQUIRE(record.FirstView < plan.Draws.size());
		SWIM_CHECK_EQUAL(plan.Draws[record.FirstView].Slot, slot);
	}
}

SWIM_TEST("Render.Shadows.Planner", "RejectsMismatchedSlotsFlagsResolutionsAndSettings")
{
	ShadowSettings settings;
	ShadowAtlasAllocator atlas(settings.AtlasSize, settings.MinTile);
	const auto camera = Camera();
	const auto plan = [&](std::vector<ShadowCasterDesc> casters)
	{
		return PlanShadows(settings, camera, casters, atlas);
	};
	auto wrongIndex = Caster(LightType::Spot, 3, 0.0f);
	wrongIndex.Light.ShadowIndex = 4;
	SWIM_CHECK_THROWS(plan({ wrongIndex }), std::invalid_argument);
	auto noFlag = Caster(LightType::Spot, 3, 0.0f);
	noFlag.Light.Flags = 0;
	SWIM_CHECK_THROWS(plan({ noFlag }), std::invalid_argument);
	SWIM_CHECK_THROWS(plan({ Caster(LightType::Spot, 64, 0.0f) }), std::invalid_argument);
	SWIM_CHECK_THROWS(plan({ Caster(LightType::Spot, 3, 0.0f), Caster(LightType::Point, 3, 1.0f) }), std::invalid_argument);
	SWIM_CHECK_THROWS(plan({ Caster(LightType::Spot, 3, 0.0f, 300) }), std::invalid_argument);
	SWIM_CHECK_THROWS(plan({ Caster(LightType::Spot, 3, 0.0f, 8192) }), std::invalid_argument);

	ShadowAtlasAllocator other(2048, 128);
	const std::vector<ShadowCasterDesc> none;
	SWIM_CHECK_THROWS(PlanShadows(settings, camera, none, other), std::invalid_argument);
	auto badCascades = settings;
	badCascades.Cascades.Count = 5;
	SWIM_CHECK_THROWS(PlanShadows(badCascades, camera, none, atlas), std::invalid_argument);
	auto badNear = settings;
	badNear.Near = 0.0f;
	SWIM_CHECK_THROWS(PlanShadows(badNear, camera, none, atlas), std::invalid_argument);

	// No casters: every record is None and no views are rendered.
	const auto empty = PlanShadows(settings, camera, none, atlas);
	SWIM_CHECK(empty.Views.empty() && empty.Draws.empty());
	SWIM_CHECK_EQUAL(empty.Records.size(), std::size_t(64));
	// Small requested resolutions are clamped up to MinTile.
	const auto clamped = plan({ Caster(LightType::Spot, 1, 0.0f, 32) });
	SWIM_CHECK_EQUAL(clamped.Draws.front().Tile.Size, settings.MinTile);
}
