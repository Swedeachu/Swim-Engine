#include "Engine/Systems/Renderer/Shadows/ShadowPlanner.h"
#include "Engine/Systems/Renderer/Lights/LightDesc.h"
#include "Engine/Systems/Renderer/Visibility/GpuViewRecord.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>
#include <stdexcept>

namespace Swim::Render
{
	namespace
	{
		bool IsPowerOfTwo(std::uint32_t value)
		{
			return value != 0 && (value & (value - 1)) == 0;
		}

		ShadowKind KindOf(const GpuLightRecord& light)
		{
			switch (static_cast<LightType>(light.Type))
			{
			case LightType::Directional:
				return ShadowKind::Directional;
			case LightType::Spot:
				return ShadowKind::Spot;
			default:
				return ShadowKind::Point;
			}
		}

		GpuShadowView MakeView(const Shadows::Matrix& viewProjection, const ShadowTile& tile, float texelWorldSize, bool perspective)
		{
			GpuShadowView view;
			std::copy(viewProjection.begin(), viewProjection.end(), view.ViewProjection);
			view.AtlasRect[0] = float(tile.X);
			view.AtlasRect[1] = float(tile.Y);
			view.AtlasRect[2] = float(tile.Size);
			view.AtlasRect[3] = float(tile.Size);
			view.TexelWorldSize = texelWorldSize;
			view.Perspective = perspective ? 1u : 0u;
			return view;
		}
	} // namespace

	ShadowPlan PlanShadows(const ShadowSettings& settings, const Shadows::ShadowCamera& camera, std::span<const ShadowCasterDesc> casters,
		ShadowAtlasAllocator& atlas)
	{
		if (atlas.GetAtlasSize() != settings.AtlasSize || atlas.GetMinTile() != settings.MinTile || settings.MaxSlots == 0 ||
			settings.Cascades.Count == 0 || settings.Cascades.Count > MaxShadowCascades || !(settings.Near > 0.0f))
		{
			throw std::invalid_argument(
				"Shadow settings must match the atlas and have slots, 1..MaxShadowCascades cascades and a near plane");
		}
		std::set<std::uint32_t> slots;
		for (const auto& caster : casters)
		{
			const auto resolution = caster.Resolution;
			if (caster.Slot >= settings.MaxSlots || !slots.insert(caster.Slot).second || caster.Light.ShadowIndex != caster.Slot ||
				(caster.Light.Flags & static_cast<std::uint32_t>(LightFlags::CastsShadows)) == 0 ||
				(resolution != 0 && (!IsPowerOfTwo(resolution) || resolution > settings.AtlasSize)))
			{
				throw std::invalid_argument("Shadow casters need unique in-range slots matching their light's ShadowIndex, "
											"LightFlags::CastsShadows and power-of-two resolutions");
			}
		}

		ShadowPlan plan;
		plan.AtlasSize = settings.AtlasSize;
		plan.Records.resize(settings.MaxSlots);
		plan.Stats.Casters = static_cast<std::uint32_t>(casters.size());

		// Budgets per kind, by priority (then slot).
		std::vector<std::size_t> order(casters.size());
		std::iota(order.begin(), order.end(), std::size_t(0));
		std::stable_sort(order.begin(), order.end(),
			[&](std::size_t a, std::size_t b)
			{
				if (casters[a].Priority != casters[b].Priority)
				{
					return casters[a].Priority > casters[b].Priority;
				}
				return casters[a].Slot < casters[b].Slot;
			});
		std::uint32_t directional = 0, spot = 0, point = 0;
		std::vector<std::size_t> accepted;
		std::vector<ShadowTileRequest> requests;
		for (const auto index : order)
		{
			const auto& caster = casters[index];
			const auto kind = KindOf(caster.Light);
			auto& used = kind == ShadowKind::Directional ? directional : kind == ShadowKind::Spot ? spot : point;
			const auto limit = kind == ShadowKind::Directional ? settings.MaxDirectionalShadows
				: kind == ShadowKind::Spot					   ? settings.MaxSpotShadows
															   : settings.MaxPointShadows;
			if (used >= limit)
			{
				++plan.Stats.OverBudget;
				continue;
			}
			++used;
			const std::uint32_t fallback = kind == ShadowKind::Directional ? settings.CascadeResolution
				: kind == ShadowKind::Spot								   ? settings.SpotResolution
																		   : settings.PointResolution;
			const std::uint32_t size = std::clamp(caster.Resolution ? caster.Resolution : fallback, settings.MinTile, settings.AtlasSize);
			if (!IsPowerOfTwo(size))
			{
				throw std::invalid_argument("Shadow resolutions must be powers of two");
			}
			const std::uint32_t count = kind == ShadowKind::Directional ? settings.Cascades.Count : kind == ShadowKind::Spot ? 1u : 6u;
			requests.push_back({ caster.Slot, size, count, caster.Priority });
			accepted.push_back(index);
		}

		const auto allocations = atlas.Allocate(requests);
		const auto& atlasStats = atlas.GetStats();
		plan.Stats.Placed = atlasStats.Placed;
		plan.Stats.Reused = atlasStats.Reused;
		plan.Stats.Downgraded = atlasStats.Downgraded;
		plan.Stats.Evicted = atlasStats.Evicted;

		// Records and views, in slot order for a deterministic view list.
		std::vector<std::size_t> bySlot(accepted.size());
		std::iota(bySlot.begin(), bySlot.end(), std::size_t(0));
		std::sort(bySlot.begin(), bySlot.end(),
			[&](std::size_t a, std::size_t b)
			{
				return casters[accepted[a]].Slot < casters[accepted[b]].Slot;
			});
		for (const auto a : bySlot)
		{
			const auto& allocation = allocations[a];
			if (allocation.Tiles.empty())
			{
				continue;
			}
			const auto& caster = casters[accepted[a]];
			const auto& light = caster.Light;
			const auto kind = KindOf(light);
			auto& record = plan.Records[caster.Slot];
			record.Kind = static_cast<std::uint32_t>(kind);
			record.FirstView = static_cast<std::uint32_t>(plan.Views.size());
			record.ViewCount = static_cast<std::uint32_t>(allocation.Tiles.size());
			record.PcfRadius = settings.PcfRadius;
			record.NormalBias = settings.NormalBias;
			record.SlopeBias = settings.SlopeBias;
			record.DepthBias = settings.DepthBias;
			for (int c = 0; c < 3; ++c)
			{
				record.LightPosition[c] = light.Position[c];
			}
			const std::uint32_t size = allocation.Tiles.front().Size;
			const auto add = [&](const Shadows::Matrix& viewProjection, float texelWorldSize, bool perspective, std::uint32_t index,
								 const Shadows::Float3& lodCenter)
			{
				ShadowViewPlan draw;
				draw.View = MakeView(viewProjection, allocation.Tiles[index], texelWorldSize, perspective);
				draw.Tile = allocation.Tiles[index];
				draw.Slot = caster.Slot;
				draw.Kind = kind;
				draw.Index = index;
				draw.Visibility.ViewProjection = viewProjection;
				draw.Visibility.CameraPosition = lodCenter;
				draw.Visibility.Flags = GpuViewFlags::ShadowCasters | GpuViewFlags::ResetLodHistory;
				plan.Views.push_back(draw.View);
				plan.Draws.push_back(draw);
			};
			const Shadows::Float3 position{ light.Position[0], light.Position[1], light.Position[2] };
			if (kind == ShadowKind::Directional)
			{
				const Shadows::Float3 direction{ light.Direction[0], light.Direction[1], light.Direction[2] };
				const auto cascades = Shadows::ComputeCascades(camera, direction, settings.Cascades, size);
				for (std::uint32_t i = 0; i < cascades.size(); ++i)
				{
					record.CascadeFar[i] = cascades[i].Far;
					add(cascades[i].ViewProjection, cascades[i].TexelWorldSize, false, i, cascades[i].Sphere.Center);
				}
			}
			else if (kind == ShadowKind::Spot)
			{
				const float fov = Shadows::SpotShadowFov(light);
				add(Shadows::SpotShadowViewProjection(light, settings.Near), 2.0f * std::tan(fov * 0.5f) / float(size), true, 0, position);
			}
			else
			{
				const auto faces = Shadows::PointShadowViewProjections(position, settings.Near);
				for (std::uint32_t face = 0; face < 6; ++face)
				{
					add(faces[face], 2.0f / float(size), true, face, position);
				}
			}
		}
		plan.Stats.Views = static_cast<std::uint32_t>(plan.Views.size());
		return plan;
	}
} // namespace Swim::Render
